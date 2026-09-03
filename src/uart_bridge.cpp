#include "uart_bridge.hpp"
#include "scoped_irq_mask.hpp"

extern "C" {
#include <string.h>
}

namespace
{
constexpr uint32_t kTxDmaAllFlags = DMA_GIF1 | DMA_TCIF1 | DMA_HTIF1 | DMA_TEIF1;
constexpr uint32_t kRxDmaAllFlags = DMA_GIF8 | DMA_TCIF8 | DMA_HTIF8 | DMA_TEIF8;
constexpr uint16_t kTxDmaConfig = DMA_CFGR1_MINC | DMA_CFGR1_DIR |
                                  DMA_CFGR1_TCIE | DMA_CFGR1_TEIE |
                                  DMA_CFGR1_PL_1;
constexpr uint16_t kRxDmaConfig = DMA_CFGR1_MINC | DMA_CFGR1_CIRC |
                                  DMA_CFGR1_HTIE | DMA_CFGR1_TCIE |
                                  DMA_CFGR1_TEIE | DMA_CFGR1_PL_1 |
                                  DMA_CFGR1_PL_0;
constexpr uint16_t kReceiveErrors = USART_STATR_ORE | USART_STATR_NE |
                                    USART_STATR_FE | USART_STATR_PE;
constexpr uint8_t kDefaultLineCoding[] = {
    0x00, 0xc2, 0x01, 0x00, // 115200 baud, little-endian
    0x00, 0x00, 0x08        // 8N1
};

inline void dmaFence()
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}
}

UartBridge* UartBridge::self_ = nullptr;

void UartBridge::configurePins(bool enabled)
{
    if (!enabled)
    {
        funPinMode(PB0, GPIO_CFGLR_IN_FLOAT);
        funPinMode(PB1, GPIO_CFGLR_IN_FLOAT);
        return;
    }

    funDigitalWrite(PB0, FUN_HIGH);
    funPinMode(PB0, GPIO_CFGLR_OUT_10Mhz_AF_PP);
    funDigitalWrite(PB1, FUN_HIGH);
    funPinMode(PB1, GPIO_CFGLR_IN_PUPD);
}

bool UartBridge::decodeLineCoding(const uint8_t* coding, uint16_t& ctlr1,
                                  uint16_t& ctlr2, uint16_t& divider) const
{
    if (!coding) return false;

    const uint32_t baud = static_cast<uint32_t>(coding[0]) |
                          (static_cast<uint32_t>(coding[1]) << 8u) |
                          (static_cast<uint32_t>(coding[2]) << 16u) |
                          (static_cast<uint32_t>(coding[3]) << 24u);
    if (baud == 0) return false;

    const uint32_t divisor = (FUNCONF_SYSTEM_CORE_CLOCK + baud / 2u) / baud;
    if (divisor < 16u || divisor > 0xffffu) return false;
    divider = static_cast<uint16_t>(divisor);

    switch (coding[4])
    {
        case 0: ctlr2 = USART_StopBits_1; break;
        case 1: ctlr2 = USART_StopBits_1_5; break;
        case 2: ctlr2 = USART_StopBits_2; break;
        default: return false;
    }

    uint16_t parity;
    switch (coding[5])
    {
        case 0: parity = USART_Parity_No; break;
        case 1: parity = USART_Parity_Odd; break;
        case 2: parity = USART_Parity_Even; break;
        default: return false;
    }

    uint16_t wordLength;
    if (coding[6] == 8)
    {
        wordLength = parity == USART_Parity_No ? USART_WordLength_8b
                                                : USART_WordLength_9b;
    }
    else if (coding[6] == 7 && parity != USART_Parity_No)
    {
        wordLength = USART_WordLength_8b;
    }
    else
    {
        return false;
    }

    ctlr1 = wordLength | parity | USART_Mode_Rx | USART_Mode_Tx;
    return true;
}

void UartBridge::applyLineCoding()
{
    uint16_t ctlr1 = 0;
    uint16_t ctlr2 = 0;
    uint16_t divider = 0;
    if (!decodeLineCoding(lineCoding_, ctlr1, ctlr2, divider)) return;

    USART4->CTLR1 = 0;
    USART4->CTLR2 = ctlr2;
    USART4->CTLR3 = USART_HardwareFlowControl_None;
    USART4->BRR = divider;
    USART4->CTLR1 = ctlr1 | CTLR1_UE_Set;
}

void UartBridge::purgeRxRegister()
{
    // STATR followed by DATAR clears RXNE and the latched receive errors.
    (void)USART4->STATR;
    (void)USART4->DATAR;
}

void UartBridge::configureDma()
{
    // USART4 TX is fixed to DMA1 channel 1 on the CH32X035.
    DMA1_Channel1->CFGR = 0;
    DMA1_Channel1->PADDR = reinterpret_cast<uint32_t>(&USART4->DATAR);
    DMA1_Channel1->MADDR = reinterpret_cast<uint32_t>(tx_);
    DMA1_Channel1->CNTR = 0;
    DMA1_Channel1->CFGR = kTxDmaConfig;

    // USART4 RX is fixed to channel 8 and continuously wraps around rx_.
    DMA1_Channel8->CFGR = 0;
    DMA1_Channel8->PADDR = reinterpret_cast<uint32_t>(&USART4->DATAR);
    DMA1_Channel8->MADDR = reinterpret_cast<uint32_t>(rx_);
    DMA1_Channel8->CNTR = kRxRingSize;
    DMA1_Channel8->CFGR = kRxDmaConfig;
    dmaFence();
    // HT flags may latch even where their interrupt is disabled.
    DMA1->INTFCR = kTxDmaAllFlags | kRxDmaAllFlags;
}

void UartBridge::stopDataPathLocked()
{
    // Stop new UART DMA requests and receive-error interrupts first.
    USART4->CTLR3 &= static_cast<uint16_t>(~(USART_CTLR3_DMAT |
                                             USART_CTLR3_DMAR |
                                             USART_CTLR3_EIE));
    USART4->CTLR1 &= static_cast<uint16_t>(~USART_CTLR1_PEIE);
    DMA1_Channel1->CFGR &= static_cast<uint16_t>(~DMA_CFGR1_EN);
    DMA1_Channel8->CFGR &= static_cast<uint16_t>(~DMA_CFGR1_EN);
    DMA1->INTFCR = kTxDmaAllFlags | kRxDmaAllFlags;

    txHead_ = txTail_ = 0;
    txDmaLength_ = 0;
    txDmaActive_ = false;
    rxCycles_ = 0;
    rxConsumed_ = 0;
    rxPaused_ = false;
    rxResetPending_ = false;
    purgeRxRegister();
}

void UartBridge::restartDataPath()
{
    ScopedIrqMask txIrq(DMA1_Channel1_IRQn);
    ScopedIrqMask rxIrq(DMA1_Channel8_IRQn);
    ScopedIrqMask usartIrq(USART4_IRQn);

    stopDataPathLocked();
    USART4->CTLR1 = 0;
    if (!active_ || !started_) return;

    applyLineCoding();
    configureDma();
    purgeRxRegister();
    // RX runs continuously; TX is armed later when its ring becomes non-empty.
    DMA1_Channel8->CFGR |= DMA_CFGR1_EN;
    USART4->CTLR1 |= USART_CTLR1_PEIE;
    USART4->CTLR3 |= USART_CTLR3_DMAT | USART_CTLR3_DMAR | USART_CTLR3_EIE;
}

void UartBridge::init()
{
    self_ = this;
    funGpioInitAll();
    RCC->APB1PCENR |= RCC_APB1Periph_USART4;
    RCC->AHBPCENR |= RCC_AHBPeriph_DMA1;
    AFIO->PCFR1 &= ~AFIO_PCFR1_USART4_REMAP;

    memcpy(lineCoding_, kDefaultLineCoding, sizeof(lineCoding_));
    configurePins(true);
    active_ = true;
    started_ = true;
    restartDataPath();

    // IRQ ownership starts here; later critical sections restore this state.
    NVIC_EnableIRQ(USART4_IRQn);
    NVIC_EnableIRQ(DMA1_Channel8_IRQn);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

void UartBridge::suspend()
{
    if (!active_) return;

    active_ = false;
    restartDataPath();
    configurePins(false);
}

void UartBridge::resume()
{
    if (active_) return;

    configurePins(true);
    active_ = true;
    restartDataPath();
}

void UartBridge::start()
{
    started_ = true;
    restartDataPath();
}

void UartBridge::stop()
{
    started_ = false;
    restartDataPath();
}

bool UartBridge::setLineCoding(const uint8_t* coding, size_t length)
{
    if (length != sizeof(lineCoding_)) return false;

    uint16_t ctlr1 = 0;
    uint16_t ctlr2 = 0;
    uint16_t divider = 0;
    if (!decodeLineCoding(coding, ctlr1, ctlr2, divider)) return false;
    if (memcmp(lineCoding_, coding, sizeof(lineCoding_)) == 0) return true;

    memcpy(lineCoding_, coding, sizeof(lineCoding_));
    restartDataPath();
    return true;
}

void UartBridge::startTxDma()
{
    if (txDmaActive_ || txTail_ == txHead_ || !active_ || !started_) return;

    // A running transfer is never extended; arrivals are picked up by its TC IRQ.
    const uint16_t offset = static_cast<uint16_t>(txTail_ & kTxQueueMask);
    uint32_t length = txHead_ - txTail_;
    const uint16_t contiguous = static_cast<uint16_t>(kTxQueueSize - offset);
    if (length > contiguous) length = contiguous;

    DMA1_Channel1->CFGR &= static_cast<uint16_t>(~DMA_CFGR1_EN);
    DMA1->INTFCR = kTxDmaAllFlags;
    DMA1_Channel1->MADDR = reinterpret_cast<uint32_t>(&tx_[offset]);
    DMA1_Channel1->CNTR = static_cast<uint16_t>(length);
    txDmaLength_ = static_cast<uint16_t>(length);
    txDmaActive_ = true;
    dmaFence();
    DMA1_Channel1->CFGR = kTxDmaConfig | DMA_CFGR1_EN;
}

bool UartBridge::writeTx(const uint8_t* data, size_t length)
{
    if ((!data && length != 0) || !active_ || !started_) return false;

    const uint32_t used = txHead_ - txTail_;
    if (used > kTxQueueSize || length > kTxQueueSize - used) return false;

    // Main context is the sole producer. The DMA IRQ may only advance txTail_,
    // which can make this capacity check conservative but never unsafe.
    uint32_t head = txHead_;
    const uint8_t dataMask = lineCoding_[6] == 7 ? 0x7fu : 0xffu;
    for (size_t i = 0; i < length; ++i)
        tx_[head++ & kTxQueueMask] = data[i] & dataMask;

    dmaFence();
    ScopedIrqMask txIrq(DMA1_Channel1_IRQn);
    txHead_ = head;
    if (length != 0) startTxDma();
    return true;
}

bool UartBridge::txDrained() const
{
    if (!active_ || !started_) return true;
    return txHead_ == txTail_ && !txDmaActive_ &&
           (USART4->STATR & USART_STATR_TC) != 0;
}

void UartBridge::resetTxAfterError()
{
    DMA1_Channel1->CFGR &= static_cast<uint16_t>(~DMA_CFGR1_EN);
    txTail_ = txHead_;
    txDmaLength_ = 0;
    txDmaActive_ = false;
    DMA1_Channel1->CNTR = 0;
    DMA1_Channel1->MADDR = reinterpret_cast<uint32_t>(tx_);
    DMA1_Channel1->CFGR = kTxDmaConfig;
}

void UartBridge::onTxDmaIrq()
{
    const uint32_t flags = DMA1->INTFR;
    if ((flags & kTxDmaAllFlags) == 0) return;

    DMA1_Channel1->CFGR &= static_cast<uint16_t>(~DMA_CFGR1_EN);
    DMA1->INTFCR = kTxDmaAllFlags;

    if (flags & DMA_TEIF1)
    {
        // The transferred prefix is unknown: discard the queue to avoid replay.
        resetTxAfterError();
        return;
    }

    // TC confirms that the complete ring slice programmed in startTxDma() left RAM.
    if ((flags & DMA_TCIF1) && txDmaActive_)
    {
        txTail_ += txDmaLength_;
        txDmaLength_ = 0;
        txDmaActive_ = false;
        startTxDma();
    }
}

uint32_t UartBridge::rxProducedSnapshot()
{
    ScopedIrqMask rxIrq(DMA1_Channel8_IRQn);
    uint32_t cycles = rxCycles_;
    const uint32_t flagsBefore = DMA1->INTFR;
    uint16_t remaining = DMA1_Channel8->CNTR;
    const uint32_t flagsAfter = DMA1->INTFR;

    // If TC appeared while CNTR was sampled, read CNTR again after the circular
    // reload. Otherwise the old value would be combined with the new cycle.
    if (!(flagsBefore & DMA_TCIF8) && (flagsAfter & DMA_TCIF8))
        remaining = DMA1_Channel8->CNTR;
    if (flagsAfter & DMA_TCIF8)
    {
        // A zero count may be observed between terminal count and circular reload.
        if (remaining == 0) remaining = kRxRingSize;
        ++cycles;
    }

    // Order the DMA cursor snapshot before CPU reads from the RX ring.
    dmaFence();
    const uint32_t produced = cycles * kRxRingSize +
                              static_cast<uint16_t>(kRxRingSize - remaining);
    return produced;
}

void UartBridge::pauseRx()
{
    // Freeze the DMA cursor in place; the channel itself remains ready to resume.
    USART4->CTLR3 &= static_cast<uint16_t>(~(USART_CTLR3_DMAR | USART_CTLR3_EIE));
    USART4->CTLR1 &= static_cast<uint16_t>(~USART_CTLR1_PEIE);
    rxPaused_ = true;
}

void UartBridge::resetRxDma()
{
    ScopedIrqMask rxIrq(DMA1_Channel8_IRQn);
    ScopedIrqMask usartIrq(USART4_IRQn);

    // Unlike pauseRx(), an error reset discards the buffered RX stream.
    USART4->CTLR3 &= static_cast<uint16_t>(~(USART_CTLR3_DMAR | USART_CTLR3_EIE));
    USART4->CTLR1 &= static_cast<uint16_t>(~USART_CTLR1_PEIE);
    DMA1_Channel8->CFGR &= static_cast<uint16_t>(~DMA_CFGR1_EN);
    DMA1->INTFCR = kRxDmaAllFlags;
    rxCycles_ = 0;
    rxConsumed_ = 0;
    rxPaused_ = false;
    rxResetPending_ = false;
    purgeRxRegister();
    DMA1_Channel8->MADDR = reinterpret_cast<uint32_t>(rx_);
    DMA1_Channel8->PADDR = reinterpret_cast<uint32_t>(&USART4->DATAR);
    DMA1_Channel8->CNTR = kRxRingSize;
    DMA1_Channel8->CFGR = kRxDmaConfig;
    if (active_ && started_)
    {
        DMA1_Channel8->CFGR |= DMA_CFGR1_EN;
        USART4->CTLR1 |= USART_CTLR1_PEIE;
        USART4->CTLR3 |= USART_CTLR3_DMAR | USART_CTLR3_EIE;
    }
}

void UartBridge::resumeRxIfPossible()
{
    if (!rxPaused_ || rxResetPending_ || !active_ || !started_) return;

    const uint32_t produced = rxProducedSnapshot();
    const uint32_t used = produced - rxConsumed_;
    if (used > kRxRingSize ||
        kRxRingSize - used < kRxHalfSize + kRxSafetyWatermark)
        return;

    ScopedIrqMask usartIrq(USART4_IRQn);
    const uint16_t status = USART4->STATR;
    if (status & kReceiveErrors) purgeRxRegister();
    // Re-enable requests without touching the preserved DMA address or count.
    rxPaused_ = false;
    USART4->CTLR1 |= USART_CTLR1_PEIE;
    USART4->CTLR3 |= USART_CTLR3_DMAR | USART_CTLR3_EIE;
}

size_t UartBridge::readRx(uint8_t* data, size_t capacity)
{
    if (!data || capacity == 0 || !active_ || !started_) return 0;
    if (rxResetPending_)
    {
        resetRxDma();
        return 0;
    }

    const uint32_t produced = rxProducedSnapshot();
    const uint32_t consumed = rxConsumed_;
    const uint32_t available = produced - consumed;
    if (available > kRxRingSize)
    {
        resetRxDma();
        return 0;
    }

    size_t length = available;
    if (length > capacity) length = capacity;
    const uint8_t dataMask = lineCoding_[6] == 7 ? 0x7fu : 0xffu;
    for (size_t i = 0; i < length; ++i)
        data[i] = rx_[(consumed + i) & (kRxRingSize - 1)] & dataMask;

    // Finish CPU reads before publishing space that DMA may overwrite.
    dmaFence();
    if (rxResetPending_)
    {
        resetRxDma();
        return 0;
    }

    rxConsumed_ = consumed + length;
    resumeRxIfPossible();
    return length;
}

void UartBridge::onRxDmaIrq()
{
    const uint32_t flags = DMA1->INTFR;
    if ((flags & kRxDmaAllFlags) == 0) return;

    DMA1->INTFCR = kRxDmaAllFlags;
    if (flags & DMA_TEIF8)
    {
        DMA1_Channel8->CFGR &= static_cast<uint16_t>(~DMA_CFGR1_EN);
        pauseRx();
        rxResetPending_ = true;
        return;
    }

    // TC marks one complete turn of the circular RX ring.
    if (flags & DMA_TCIF8) ++rxCycles_;
    if (!active_ || !started_ || rxPaused_) return;

    const uint16_t remaining = DMA1_Channel8->CNTR;
    const uint16_t position = (flags & DMA_TCIF8) && remaining == 0
        ? 0
        : static_cast<uint16_t>(kRxRingSize - remaining);
    const uint32_t produced = rxCycles_ * kRxRingSize + position;
    const uint32_t used = produced - rxConsumed_;
    if (used > kRxRingSize)
    {
        pauseRx();
        rxResetPending_ = true;
        return;
    }

    // Stop one half early; the watermark absorbs IRQ latency before DMAR clears.
    if (kRxRingSize - used < kRxHalfSize + kRxSafetyWatermark)
        pauseRx();
}

void UartBridge::onUsartIrq()
{
    const uint16_t status = USART4->STATR;
    if ((status & kReceiveErrors) == 0) return;

    // A line error only makes the current character uncertain. Preserve the
    // buffered stream; full RX resets are reserved for DMA and ring failures.
    ScopedIrqMask rxIrq(DMA1_Channel8_IRQn);
    const bool resume = active_ && started_ && !rxPaused_;
    USART4->CTLR3 &= static_cast<uint16_t>(~(USART_CTLR3_DMAR | USART_CTLR3_EIE));
    USART4->CTLR1 &= static_cast<uint16_t>(~USART_CTLR1_PEIE);
    purgeRxRegister();
    if (resume)
    {
        USART4->CTLR1 |= USART_CTLR1_PEIE;
        USART4->CTLR3 |= USART_CTLR3_DMAR | USART_CTLR3_EIE;
    }
}

extern "C" void USART4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
extern "C" void USART4_IRQHandler(void)
{
    UartBridge::handleUsartIrq();
}

extern "C" void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
extern "C" void DMA1_Channel1_IRQHandler(void)
{
    UartBridge::handleTxDmaIrq();
}

extern "C" void DMA1_Channel8_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
extern "C" void DMA1_Channel8_IRQHandler(void)
{
    UartBridge::handleRxDmaIrq();
}
