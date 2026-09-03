#include "usb_wchlink.hpp"
#include "wchlink_descriptors.hpp"
#include "usb_serial.hpp"
#include "time.hpp"
#ifdef UART_BRIDGE
#include "uart_bridge.hpp"
#endif

extern "C" {
#include <string.h>
}

#define UEP_IN  0x80
#define UEP_OUT 0x00
#define UEP0    0x00
#define UEP1    0x01
#ifdef UART_BRIDGE
#define UEP5    0x05
#define UEP6    0x06
#define UEP7    0x07
#endif

#define UDM_PUE_MASK 0x00000003

namespace Desc = WchLinkUsbDescriptors;

namespace
{
constexpr uint32_t kAbandonedReplyTimeoutMs = 1000;

struct __attribute__((packed)) UsbSetupRequest
{
    uint8_t bRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

constexpr uint8_t USB_GET_STATUS = 0x00;
constexpr uint8_t USB_CLEAR_FEATURE = 0x01;
constexpr uint8_t USB_SET_ADDRESS = 0x05;
constexpr uint8_t USB_GET_DESCRIPTOR = 0x06;
constexpr uint8_t USB_GET_CONFIGURATION = 0x08;
constexpr uint8_t USB_SET_CONFIGURATION = 0x09;
constexpr uint8_t USB_GET_INTERFACE = 0x0a;
constexpr uint8_t USB_SET_INTERFACE = 0x0b;
constexpr uint8_t USB_REQ_TYP_MASK = 0x60;
constexpr uint8_t USB_REQ_TYP_STANDARD = 0x00;
constexpr uint8_t USB_REQ_TYP_CLASS = 0x20;
constexpr uint8_t USB_REQ_RECIP_MASK = 0x1f;
constexpr uint8_t USB_REQ_RECIP_ENDP = 0x02;
constexpr uint8_t USB_REQ_FEAT_ENDP_HALT = 0x00;
constexpr uint8_t USB_DESCR_TYP_DEVICE = 0x01;
constexpr uint8_t USB_DESCR_TYP_CONFIG = 0x02;
constexpr uint8_t USB_DESCR_TYP_STRING = 0x03;

#ifdef UART_BRIDGE
constexpr uint8_t CDC_SET_LINE_CODING = 0x20;
constexpr uint8_t CDC_GET_LINE_CODING = 0x21;
constexpr uint8_t CDC_SET_CONTROL_LINE_STATE = 0x22;
#endif

constexpr uint8_t USBFS_UEP1_RX_EN = RB_UEP1_RX_EN;
constexpr uint8_t USBFS_UEP1_TX_EN = RB_UEP1_TX_EN;
#ifdef UART_BRIDGE
// CH32X035 R8_UEP567_MOD bit layout (it differs from other WCH USBFS IPs).
constexpr uint8_t USBFS_UEP5_TX_EN = 1u << 0;
constexpr uint8_t USBFS_UEP6_RX_EN = 1u << 3;
constexpr uint8_t USBFS_UEP7_TX_EN = 1u << 4;
#endif
constexpr uint8_t USBFS_UD_PD_DIS = RB_UD_PD_DIS;
constexpr uint8_t USBFS_UD_PORT_EN = RB_UD_PORT_EN;
constexpr uint8_t USBFS_UIF_TRANSFER = RB_UIF_TRANSFER;
constexpr uint8_t USBFS_UIF_BUS_RST = RB_UIF_BUS_RST;
constexpr uint8_t USBFS_UIF_SUSPEND = RB_UIF_SUSPEND;
constexpr uint8_t USBFS_UIS_TOKEN_MASK = MASK_UIS_TOKEN;
constexpr uint8_t USBFS_UIS_TOKEN_IN = UIS_TOKEN_IN;
constexpr uint8_t USBFS_UIS_TOKEN_OUT = UIS_TOKEN_OUT;
constexpr uint8_t USBFS_UIS_TOKEN_SETUP = UIS_TOKEN_SETUP;
constexpr uint8_t USBFS_UIS_ENDP_MASK = MASK_UIS_ENDP;
constexpr uint8_t USBFS_UIS_TOG_OK = RB_UIS_TOG_OK;
}

UsbWchLink* UsbWchLink::self_ = nullptr;

void UsbWchLink::endpointInit()
{
    // EP1 bidirectional: enable both RX (OUT) and TX (IN) on endpoint 1.
    USBFSD->UEP4_1_MOD = USBFS_UEP1_RX_EN | USBFS_UEP1_TX_EN;
    USBFSD->UEP2_3_MOD = 0;
    USBFSD->UEP567_MOD = 0;

    USBFSD->UEP0_DMA = (uint32_t)ep0_;
    USBFSD->UEP1_DMA = (uint32_t)ep1_;
#ifdef UART_BRIDGE
    if (uart_)
    {
        USBFSD->UEP567_MOD = USBFS_UEP5_TX_EN | USBFS_UEP6_RX_EN |
                             USBFS_UEP7_TX_EN;
        USBFSD->UEP5_DMA = (uint32_t)ep5Notify_;
        USBFSD->UEP6_DMA = (uint32_t)ep6Out_;
        USBFSD->UEP7_DMA = (uint32_t)ep7In_;
    }
#endif

    USBFSD->UEP0_CTRL_H = USBFS_UEP_R_RES_ACK | USBFS_UEP_T_RES_NAK;
    resetEp1(true, true);
#ifdef UART_BRIDGE
    if (uart_) resetCdcEndpoints(true, true);
#endif
}

#ifdef UART_BRIDGE
void UsbWchLink::resetCdcEndpoints(bool resetOutToggle, bool resetInToggle)
{
    uint16_t outToggle = USBFSD->UEP6_CTRL_H & USBFS_UEP_R_TOG;
    uint16_t inToggle = USBFSD->UEP7_CTRL_H & USBFS_UEP_T_TOG;
    if (resetOutToggle) outToggle = 0;
    if (resetInToggle) inToggle = 0;

    USBFSD->UEP5_TX_LEN = 0;
    USBFSD->UEP6_TX_LEN = 0;
    USBFSD->UEP7_TX_LEN = 0;
    USBFSD->UEP5_CTRL_H = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP6_CTRL_H = outToggle | USBFS_UEP_R_RES_ACK;
    USBFSD->UEP7_CTRL_H = inToggle | USBFS_UEP_T_RES_NAK;
    cdcOutPendingLength_ = 0;
    cdcOutPending_ = false;
    cdcInBusy_ = false;
    cdcInNeedsZlp_ = false;
    cdcActivityPending_ = false;
    uartRxDrainPending_ = false;
    cdcStopPending_ = false;
}

bool UsbWchLink::queueCdcInFromIrq()
{
    if (!uart_ || !configured_ || !cdcSessionOpen_ || controlOutStatusPending_ ||
        cdcInBusy_)
        return false;

    // Drain one packet from UART RX directly into the CDC IN DMA buffer.
    const size_t length = uart_->readRx(ep7In_, kPacketSize);
    if (length == 0 && !cdcInNeedsZlp_) return false;

    // A full-size bulk packet does not terminate the host's USB transfer.
    // If no byte follows it, send a ZLP so Linux cdc-acm can complete its URB.
    cdcInNeedsZlp_ = length == kPacketSize;

    USBFSD->UEP7_TX_LEN = static_cast<uint16_t>(length);
    USBFSD->UEP7_CTRL_H = (USBFSD->UEP7_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                          USBFS_UEP_T_RES_ACK;
    cdcInBusy_ = true;
    return true;
}

void UsbWchLink::releaseControlOutBarrier()
{
    if (!controlOutStatusPending_) return;

    controlOutStatusPending_ = false;
    if (txBusy_)
        USBFSD->UEP1_CTRL_H =
            (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
            USBFS_UEP_T_RES_ACK;
    if (cdcInBusy_)
        USBFSD->UEP7_CTRL_H =
            (USBFSD->UEP7_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
            USBFS_UEP_T_RES_ACK;
}

#endif

void UsbWchLink::resetEp1(bool resetOutToggle, bool resetInToggle)
{
    uint16_t toggles = USBFSD->UEP1_CTRL_H &
                       (USBFS_UEP_R_TOG | USBFS_UEP_T_TOG);
    if (resetOutToggle) toggles &= (uint16_t)~USBFS_UEP_R_TOG;
    if (resetInToggle) toggles &= (uint16_t)~USBFS_UEP_T_TOG;

    // Drop both halves of the paired command/reply transaction. Reconfiguration
    // resets both directions to DATA0; CLEAR_FEATURE resets only its endpoint;
    // timeout/suspend recovery preserves both bus data toggles.
    USBFSD->UEP1_TX_LEN = 0;
    USBFSD->UEP1_CTRL_H = toggles | USBFS_UEP_R_RES_ACK | USBFS_UEP_T_RES_NAK;
    pendingLength_ = 0;
    packetPending_ = false;
    packetTaken_ = false;
    txBusy_ = false;
    txStartedMs_ = 0;
}

void UsbWchLink::init(UartBridge* uart)
{
    self_ = this;
#ifdef UART_BRIDGE
    uart_ = uart;
    if (uart_) uart_->stop();
#else
    (void)uart;
    uart_ = nullptr;
#endif
    sessionResetPending_ = false;

    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOC;
    RCC->AHBPCENR |= RCC_AHBPeriph_USBFS;

    GPIOC->BSXR = GPIO_Pin_17 >> 16u;
    GPIOC->CFGXR = (GPIOC->CFGXR & ~0xffu) |
                   (GPIO_CFGLR_IN_FLOAT << 0) |
                   (GPIO_CFGLR_IN_PUPD << 4);

    AFIO->CTLR = (AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK)) |
                 USB_PHY_V33 | UDP_PUE_1K5 | USB_IOEN;

    USBFSD->BASE_CTRL = 0;
    endpointInit();
    USBFSD->DEV_ADDR = 0;
    USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
    USBFSD->INT_FG = 0xFF;
    USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
    USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;

    NVIC_EnableIRQ(USBFS_IRQn);
}

bool UsbWchLink::pollCdc()
{
#ifndef UART_BRIDGE
    return false;
#else
    if (!uart_ || !configured_) return false;

    NVIC_DisableIRQ(USBFS_IRQn);
    bool activity = cdcActivityPending_;
    cdcActivityPending_ = false;
    if (!controlOutStatusPending_ && cdcOutPending_ &&
        uart_->writeTx(ep6Out_, cdcOutPendingLength_))
    {
        cdcOutPending_ = false;
        cdcOutPendingLength_ = 0;
        if (cdcSessionOpen_ && !cdcStopPending_)
            USBFSD->UEP6_CTRL_H =
                (USBFSD->UEP6_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                USBFS_UEP_R_RES_ACK;
        activity = true;
    }

    // DTR close is handled in main context: first deliver any OUT packet that
    // USB already acknowledged, then wait for the UART's final stop bit.
    if (cdcStopPending_ && !cdcOutPending_ && uart_->txDrained())
    {
        uart_->stop();
        cdcStopPending_ = false;
        if (cdcSessionOpen_)
        {
            uart_->start();
            USBFSD->UEP6_CTRL_H =
                (USBFSD->UEP6_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                USBFS_UEP_R_RES_ACK;
        }
    }

    NVIC_EnableIRQ(USBFS_IRQn);
    return activity;
#endif
}

#ifdef UART_BRIDGE
void UsbWchLink::requestUartRxDrain()
{
    if (!uart_ || !configured_ || !cdcSessionOpen_ || cdcStopPending_ ||
        cdcInBusy_ || uartRxDrainPending_ || (sleepStatus_ & 0x02))
        return;

    uartRxDrainPending_ = true;
    NVIC_SetPendingIRQ(USBFS_IRQn);
}
#endif

void UsbWchLink::armOut()
{
    USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                          USBFS_UEP_R_RES_ACK;
}

bool UsbWchLink::takeNextPacket(uint8_t* destination, size_t& length)
{
    bool available = false;
    NVIC_DisableIRQ(USBFS_IRQn);
    if (packetPending_ && !packetTaken_)
    {
        length = pendingLength_;
        memcpy(destination, ep1_, length);
        packetTaken_ = true;
        available = true;
    }
    NVIC_EnableIRQ(USBFS_IRQn);
    return available;
}

bool UsbWchLink::takeSessionReset()
{
    bool pending;
    NVIC_DisableIRQ(USBFS_IRQn);
    const bool replyTimedOut = txBusy_ &&
        (uint32_t)(Time::millis() - txStartedMs_) >= kAbandonedReplyTimeoutMs;
    pending = sessionResetPending_ || replyTimedOut;
    sessionResetPending_ = false;
    if (pending)
    {
        resetEp1(false, false);
    }
    NVIC_EnableIRQ(USBFS_IRQn);
    return pending;
}

bool UsbWchLink::finish(const uint8_t* response, size_t length)
{
    if (length == 0 || length > kPacketSize) return false;

    bool accepted = false;
    NVIC_DisableIRQ(USBFS_IRQn);
    if (packetPending_ && packetTaken_ && !txBusy_)
    {
        packetPending_ = false;
        packetTaken_ = false;
        pendingLength_ = 0;

        // TX region is the upper half of the 128-byte EP1 buffer (RX is [0..63]),
        // so the reply never aliases the received command. EP1 OUT stays NAKed
        // until the IN completes.
        memcpy(ep1_ + kEp1TxOffset, response, length);
        USBFSD->UEP1_TX_LEN = (uint16_t)length;
#ifdef UART_BRIDGE
        if (!controlOutStatusPending_)
#endif
            USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                                  USBFS_UEP_T_RES_ACK;
        txBusy_ = true;
        txStartedMs_ = Time::millis();
        accepted = true;
    }
    NVIC_EnableIRQ(USBFS_IRQn);
    return accepted;
}

void UsbWchLink::handleSetup()
{
#ifdef UART_BRIDGE
    // A new SETUP aborts any previous control transfer, including a missing
    // SET_LINE_CODING status stage. Release the bulk IN endpoints it blocked.
    releaseControlOutBarrier();
#endif
    const auto* request = reinterpret_cast<const UsbSetupRequest*>(ep0_);
    uint16_t length = 0;
    bool error = false;

    USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK |
                          USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;

    descriptorCursor_ = nullptr;
    setupRequestType_ = request->bRequestType;
    setupRequest_ = request->bRequest;
    setupLength_ = request->wLength;
    setupValue_ = request->wValue;
    setupIndex_ = request->wIndex;

#ifdef UART_BRIDGE
    cdcLineCodingPending_ = false;
#endif

    if ((setupRequestType_ & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS)
    {
#ifdef UART_BRIDGE
        if (setupIndex_ != Desc::kCdcControlInterface || !uart_)
        {
            error = true;
        }
        else
        {
            switch (setupRequest_)
            {
                case CDC_SET_LINE_CODING:
                    if ((setupRequestType_ & UEP_IN) != 0 || setupLength_ != 7)
                        error = true;
                    else
                        cdcLineCodingPending_ = true;
                    break;

                case CDC_GET_LINE_CODING:
                    if ((setupRequestType_ & UEP_IN) == 0)
                    {
                        error = true;
                        break;
                    }
                    if (setupLength_ > 7) setupLength_ = 7;
                    memcpy(ep0_, uart_->lineCoding(), setupLength_);
                    break;

                case CDC_SET_CONTROL_LINE_STATE:
                    if ((setupRequestType_ & UEP_IN) != 0 || setupLength_ != 0)
                        error = true;
                    else
                    {
                        const bool dtr = (setupValue_ & 1u) != 0;
                        if (dtr != cdcSessionOpen_)
                        {
                            cdcSessionOpen_ = dtr;
                            if (dtr)
                            {
                                // If a close is draining, pollCdc() performs a
                                // clean stop/start before accepting more OUT.
                                if (!cdcStopPending_)
                                {
                                    uart_->start();
                                    USBFSD->UEP6_CTRL_H =
                                        (USBFSD->UEP6_CTRL_H &
                                         ~USBFS_UEP_R_RES_MASK) |
                                        USBFS_UEP_R_RES_ACK;
                                }
                            }
                            else
                            {
                                // Stop accepting new traffic, but retain an OUT
                                // packet that may already have been ACKed.
                                USBFSD->UEP6_CTRL_H =
                                    (USBFSD->UEP6_CTRL_H &
                                     ~USBFS_UEP_R_RES_MASK) |
                                    USBFS_UEP_R_RES_NAK;
                                USBFSD->UEP7_TX_LEN = 0;
                                USBFSD->UEP7_CTRL_H =
                                    (USBFSD->UEP7_CTRL_H &
                                     ~USBFS_UEP_T_RES_MASK) |
                                    USBFS_UEP_T_RES_NAK;
                                cdcInBusy_ = false;
                                cdcInNeedsZlp_ = false;
                                uartRxDrainPending_ = false;
                                cdcStopPending_ = true;
                            }
                        }
                    }
                    break; // RTS is intentionally not routed.

                default:
                    error = true;
                    break;
            }
        }
#else
        error = true;
#endif
    }
    else if ((setupRequestType_ & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
    {
        error = true;
    }
    else
    {
        switch (setupRequest_)
        {
            case USB_GET_DESCRIPTOR:
                switch ((uint8_t)(setupValue_ >> 8))
                {
                    case USB_DESCR_TYP_DEVICE:
                        descriptorCursor_ = uart_ ? Desc::deviceWithUart : Desc::device;
                        length = descriptorCursor_[0];
                        break;
                    case USB_DESCR_TYP_CONFIG:
                        descriptorCursor_ = uart_ ? Desc::configurationWithUart
                                                  : Desc::configuration;
                        length = Desc::configurationLength(descriptorCursor_);
                        break;
                    case USB_DESCR_TYP_STRING:
                        switch ((uint8_t)setupValue_)
                        {
                            case Desc::Lang: descriptorCursor_ = Desc::lang; length = Desc::lang[0]; break;
                            case Desc::Manufacturer: descriptorCursor_ = Desc::manufacturer; length = Desc::manufacturer[0]; break;
                            case Desc::Product: descriptorCursor_ = Desc::product; length = Desc::product[0]; break;
                            case Desc::Serial: descriptorCursor_ = UsbSerial::serialDescriptor(); length = descriptorCursor_[0]; break;
                            default: error = true; break;
                        }
                        break;
                    default:
                        error = true;
                        break;
                }
                if (!error)
                {
                    if (setupLength_ > length) setupLength_ = length;
                    length = setupLength_ > Desc::kEp0Size ? Desc::kEp0Size : setupLength_;
                    memcpy(ep0_, descriptorCursor_, length);
                    descriptorCursor_ += length;
                }
                break;

            case USB_SET_ADDRESS:
                deviceAddress_ = (uint8_t)setupValue_;
                break;

            case USB_GET_CONFIGURATION:
                ep0_[0] = deviceConfiguration_;
                if (setupLength_ > 1) setupLength_ = 1;
                break;

            case USB_SET_CONFIGURATION:
                if (setupValue_ > 1)
                {
                    error = true;
                    break;
                }
                deviceConfiguration_ = (uint8_t)setupValue_;
                configured_ = deviceConfiguration_ != 0;
                resetEp1(true, true);
#ifdef UART_BRIDGE
                if (uart_)
                {
                    resetCdcEndpoints(true, true);
                    cdcSessionOpen_ = configured_;
                    if (configured_) uart_->start();
                    else uart_->stop();
                }
#endif
                sessionResetPending_ = true;
                break;

            case USB_GET_INTERFACE:
                ep0_[0] = 0;
                if (setupLength_ > 1) setupLength_ = 1;
                break;

            case USB_SET_INTERFACE:
                if (setupValue_ != 0 || !configured_)
                {
                    error = true;
                    break;
                }
                if (setupIndex_ == 0)
                {
                    resetEp1(true, true);
                    sessionResetPending_ = true;
                }
#ifdef UART_BRIDGE
                else if (uart_ &&
                         (setupIndex_ == Desc::kCdcControlInterface ||
                          setupIndex_ == Desc::kCdcDataInterface))
                {
                    resetCdcEndpoints(true, true);
                    if (cdcSessionOpen_) uart_->start();
                    else uart_->stop();
                }
#endif
                else error = true;
                break;

            case USB_GET_STATUS:
                ep0_[0] = 0;
                ep0_[1] = 0;
                if (setupLength_ > 2) setupLength_ = 2;
                break;

            case USB_CLEAR_FEATURE:
                if ((setupRequestType_ & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP &&
                    setupValue_ == USB_REQ_FEAT_ENDP_HALT)
                {
                    switch (setupIndex_)
                    {
                        case (UEP_OUT | UEP1):
                            resetEp1(true, false);
                            sessionResetPending_ = true;
                            break;
                        case (UEP_IN | UEP1):
                            resetEp1(false, true);
                            sessionResetPending_ = true;
                            break;
#ifdef UART_BRIDGE
                        case (UEP_IN | UEP5):
                            if (!uart_) { error = true; break; }
                            USBFSD->UEP5_CTRL_H = USBFS_UEP_T_RES_NAK;
                            break;
                        case (UEP_OUT | UEP6):
                            if (!uart_) { error = true; break; }
                            resetCdcEndpoints(true, false);
                            if (cdcSessionOpen_) uart_->start();
                            else uart_->stop();
                            break;
                        case (UEP_IN | UEP7):
                            if (!uart_) { error = true; break; }
                            resetCdcEndpoints(false, true);
                            if (cdcSessionOpen_) uart_->start();
                            else uart_->stop();
                            break;
#endif
                        default: error = true; break;
                    }
                }
                else
                {
                    error = true;
                }
                break;

            default:
                error = true;
                break;
        }
    }

    if (error)
    {
        USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL |
                              USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
    }
#ifdef UART_BRIDGE
    else if (cdcLineCodingPending_)
    {
        // Receive the seven-byte DATA1 stage before acknowledging status IN.
        USBFSD->UEP0_CTRL_H = USBFS_UEP_T_RES_NAK |
                              USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
    }
#endif
    else if (setupRequestType_ & UEP_IN)
    {
        length = setupLength_ > Desc::kEp0Size ? Desc::kEp0Size : setupLength_;
        setupLength_ -= length;
        USBFSD->UEP0_TX_LEN = length;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                              USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
    }
    else
    {
        USBFSD->UEP0_TX_LEN = 0;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                              USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
    }
}

#ifdef UART_BRIDGE
void UsbWchLink::handleEp0Out()
{
    if (!cdcLineCodingPending_) return;

    cdcLineCodingPending_ = false;
    setupLength_ = 0;
    if (!uart_ || USBFSD->RX_LEN != 7 || !uart_->setLineCoding(ep0_, 7))
    {
        USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL |
                              USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
        return;
    }

    // CH32X035 USBFS must not start another IN transfer between a control OUT
    // data stage and its status IN. Preserve queued transfers and re-arm them
    // from handleEp0In once the status packet has completed.
    controlOutStatusPending_ = true;
    USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                          USBFS_UEP_T_RES_NAK;
    USBFSD->UEP7_CTRL_H = (USBFSD->UEP7_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                          USBFS_UEP_T_RES_NAK;
    USBFSD->UEP0_TX_LEN = 0;
    USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK |
                          USBFS_UEP_R_RES_NAK;
}
#endif

void UsbWchLink::handleEp0In()
{
#ifdef UART_BRIDGE
    releaseControlOutBarrier();
#endif

    if (setupLength_ == 0)
    {
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                              USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
    }

    if (setupRequest_ == USB_GET_DESCRIPTOR && setupLength_ != 0)
    {
        const uint16_t length = setupLength_ > Desc::kEp0Size ? Desc::kEp0Size : setupLength_;
        memcpy(ep0_, descriptorCursor_, length);
        descriptorCursor_ += length;
        setupLength_ -= length;
        USBFSD->UEP0_TX_LEN = length;
        USBFSD->UEP0_CTRL_H ^= USBFS_UEP_T_TOG;
    }
    else if (setupRequest_ == USB_SET_ADDRESS)
    {
        USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | deviceAddress_;
    }
}

void UsbWchLink::busReset()
{
    configured_ = false;
    deviceConfiguration_ = 0;
    deviceAddress_ = 0;
    sleepStatus_ = 0;
#ifdef UART_BRIDGE
    cdcLineCodingPending_ = false;
    cdcSessionOpen_ = false;
    controlOutStatusPending_ = false;
    if (uart_) uart_->stop();
#endif
    USBFSD->DEV_ADDR = 0;
    endpointInit();
    sessionResetPending_ = true;
}

void UsbWchLink::onIrq()
{
    const uint8_t flags = USBFSD->INT_FG;
    const uint8_t status = USBFSD->INT_ST;

    if (flags & USBFS_UIF_TRANSFER)
    {
        switch (status & USBFS_UIS_TOKEN_MASK)
        {
            case USBFS_UIS_TOKEN_IN:
                if ((status & USBFS_UIS_ENDP_MASK) == UEP0)
                {
                    handleEp0In();
                }
                else if ((status & USBFS_UIS_ENDP_MASK) == UEP1)
                {
                    // Reply delivered: NAK further IN, release TX, re-arm OUT.
                    USBFSD->UEP1_CTRL_H ^= USBFS_UEP_T_TOG;
                    USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                                          USBFS_UEP_T_RES_NAK;
                    txBusy_ = false;
                    txStartedMs_ = 0;
                    armOut();
                }
#ifdef UART_BRIDGE
                else if (uart_ && (status & USBFS_UIS_ENDP_MASK) == UEP7)
                {
                    USBFSD->UEP7_CTRL_H ^= USBFS_UEP_T_TOG;
                    USBFSD->UEP7_CTRL_H = (USBFSD->UEP7_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                                          USBFS_UEP_T_RES_NAK;
                    cdcInBusy_ = false;
                    cdcActivityPending_ = true;
                    // Keep CDC IN flowing even while main handles a synchronous
                    // WCH command. The periodic tick seeds a new idle chain.
                    queueCdcInFromIrq();
                }
#endif
                break;

            case USBFS_UIS_TOKEN_OUT:
#ifdef UART_BRIDGE
                if ((status & USBFS_UIS_ENDP_MASK) == UEP0 &&
                    (status & USBFS_UIS_TOG_OK))
                {
                    handleEp0Out();
                }
                else
#endif
                if ((status & USBFS_UIS_ENDP_MASK) == UEP1 &&
                    (status & USBFS_UIS_TOG_OK))
                {
                    // Stop receiving until the command is handled (shared buffer).
                    USBFSD->UEP1_CTRL_H ^= USBFS_UEP_R_TOG;
                    USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                                          USBFS_UEP_R_RES_NAK;
                    pendingLength_ = (uint8_t)USBFSD->RX_LEN;
                    packetPending_ = true;
                    packetTaken_ = false;
                }
#ifdef UART_BRIDGE
                else if (uart_ && (status & USBFS_UIS_ENDP_MASK) == UEP6 &&
                         (status & USBFS_UIS_TOG_OK))
                {
                    USBFSD->UEP6_CTRL_H ^= USBFS_UEP_R_TOG;
                    USBFSD->UEP6_CTRL_H = (USBFSD->UEP6_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                                          USBFS_UEP_R_RES_NAK;
                    cdcOutPendingLength_ = static_cast<uint8_t>(USBFSD->RX_LEN);
                    cdcOutPending_ = true;
                }
#endif
                break;

            case USBFS_UIS_TOKEN_SETUP:
                handleSetup();
                break;

            default:
                break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    }
    else if (flags & USBFS_UIF_BUS_RST)
    {
        busReset();
        USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    }
    else if (flags & USBFS_UIF_SUSPEND)
    {
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
        if (USBFSD->MIS_ST & USBFS_UMS_SUSPEND)
        {
            sleepStatus_ |= 0x02;
            sessionResetPending_ = true;
#ifdef UART_BRIDGE
            if (uart_)
            {
                resetCdcEndpoints(false, false);
                uart_->stop();
            }
#endif
        }
        else
        {
            sleepStatus_ &= (uint8_t)~0x02;
#ifdef UART_BRIDGE
            if (uart_ && configured_ && cdcSessionOpen_)
                uart_->start();
#endif
        }
    }
    else
    {
        USBFSD->INT_FG = flags;
    }

#ifdef UART_BRIDGE
    if (uartRxDrainPending_)
    {
        uartRxDrainPending_ = false;
        const bool cdcInPacketQueued = queueCdcInFromIrq();
        if (cdcInPacketQueued) cdcActivityPending_ = true;
    }
#endif
}

extern "C" void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
extern "C" void USBFS_IRQHandler(void)
{
    UsbWchLink::handleIrq();
}
