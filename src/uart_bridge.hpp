#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ch32_sdk.hpp"

// Self-contained DMA-driven USART4 transport on the CH32X035 default PB0/TX4,
// PB1/RX4 mapping. writeTx() appends to the UART TX ring; transfer-complete IRQs
// advance its tail and chain queued slices. Circular DMA continuously fills the
// UART RX ring while the pump is running; half/complete IRQs pause it before
// unread data can be overwritten, and readRx() resumes it once space is available.
// This data flow is transport-agnostic: the caller supplies and consumes bytes.
class UartBridge
{
public:
    void init();
    // suspend()/resume() isolate or restore the pins without changing whether
    // the pump should run. start()/stop() reset both rings and toggle the pump.
    void suspend();
    void resume();
    void start();
    void stop();

    // Updates UART framing from a seven-byte serial line-coding record.
    bool setLineCoding(const uint8_t* coding, size_t length);
    const uint8_t* lineCoding() const { return lineCoding_; }

    // UART TX accepts a write atomically; UART RX may return a short read.
    bool writeTx(const uint8_t* data, size_t length);
    // True only after every queued byte, including the final stop bit, has left
    // the UART. This lets USB close a CDC session without blocking in its IRQ.
    bool txDrained() const;
    size_t readRx(uint8_t* data, size_t capacity);

    void onUsartIrq();
    void onTxDmaIrq();
    void onRxDmaIrq();
    static void handleUsartIrq() { if (self_) self_->onUsartIrq(); }
    static void handleTxDmaIrq() { if (self_) self_->onTxDmaIrq(); }
    static void handleRxDmaIrq() { if (self_) self_->onRxDmaIrq(); }

private:
    static constexpr uint16_t kTxQueueSize = 1024;
    static constexpr uint16_t kTxQueueMask = kTxQueueSize - 1;
    static constexpr uint16_t kRxRingSize = 2048;
    static constexpr uint16_t kRxHalfSize = kRxRingSize / 2;
    static constexpr uint16_t kRxSafetyWatermark = 128;
    static_assert((kTxQueueSize & kTxQueueMask) == 0, "UART TX queue must be a power of two");
    static_assert((kRxRingSize & (kRxRingSize - 1)) == 0, "UART RX ring must be a power of two");
    static_assert(kRxSafetyWatermark < kRxHalfSize, "UART RX watermark must fit within one DMA half");

    bool decodeLineCoding(const uint8_t* coding, uint16_t& ctlr1,
                          uint16_t& ctlr2, uint16_t& divider) const;
    void configurePins(bool enabled);
    void applyLineCoding();
    void restartDataPath();
    // DMA TX/RX and USART4 IRQs must all be masked by the caller.
    void stopDataPathLocked();
    void configureDma();
    void startTxDma();
    void resetTxAfterError();
    void resetRxDma();
    void pauseRx();
    void resumeRxIfPossible();
    uint32_t rxProducedSnapshot();
    void purgeRxRegister();

    alignas(4) uint8_t tx_[kTxQueueSize];
    alignas(4) uint8_t rx_[kRxRingSize];
    volatile uint32_t txHead_;
    volatile uint32_t txTail_;
    volatile uint16_t txDmaLength_;
    volatile bool txDmaActive_;
    volatile uint32_t rxCycles_;
    volatile uint32_t rxConsumed_;
    volatile bool rxPaused_;
    volatile bool rxResetPending_;
    volatile bool active_;
    volatile bool started_;
    uint8_t lineCoding_[7];

    static UartBridge* self_;
};
