#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ch32_sdk.hpp"

class UartBridge;

// Interrupt-driven CH32X035 USBFS transport for the WCH-Link personality.
// Vendor interface 0 uses a bidirectional bulk endpoint 1: OUT 0x01 and IN 0x81
// share one DMA buffer (WCH single-buffer mode). A non-null UART pointer adds
// CDC ACM interfaces 1/2. Command decoding runs in main context while EP1 OUT
// is NAKed.
class UsbWchLink
{
public:
    static constexpr size_t kPacketSize = 64;

    void init(UartBridge* uart = nullptr);
    bool configured() const { return configured_; }

    // CDC bridge orchestration lives here: main moves complete CDC OUT packets
    // into UART TX, while the system tick requests a UART RX drain in the USB
    // IRQ. Each completed CDC IN transfer immediately queues the next packet.
    bool pollCdc();
#ifdef UART_BRIDGE
    // Defers the actual UART RX read and CDC IN submission to USBFS_IRQHandler.
    void requestUartRxDrain();
#endif

    // Copies the pending OUT command. EP1 OUT stays NAKed until finish().
    bool takeNextPacket(uint8_t* destination, size_t& length);

    // USB reset or suspend released the session; main must release the target.
    bool takeSessionReset();

    // Sends the reply on EP1 IN, then re-arms EP1 OUT once the IN completes.
    // length must be >= 1 (the decoder guarantees a non-empty reply).
    bool finish(const uint8_t* response, size_t length);

    void onIrq();
    static void handleIrq() { if (self_) self_->onIrq(); }

private:
    void endpointInit();
    void resetEp1(bool resetOutToggle, bool resetInToggle);
#ifdef UART_BRIDGE
    void resetCdcEndpoints(bool resetOutToggle, bool resetInToggle);
    // Called only from USBFS_IRQHandler, which serializes CDC endpoint state.
    bool queueCdcInFromIrq();
    void releaseControlOutBarrier();
#endif
    void handleSetup();
    void handleEp0In();
#ifdef UART_BRIDGE
    void handleEp0Out();
#endif
    void busReset();
    void armOut();

    alignas(4) uint8_t ep0_[kPacketSize];
    // EP1 bidirectional buffer. With RX_EN|TX_EN and BUF_MOD=0 the CH32X035
    // USBFS lays out 128 bytes at UEP1_DMA: OUT (RX) in [0..63], IN (TX) in
    // [64..127] (ch32x035_usb.h endpoint-mode table). TX must be written at +64.
    alignas(4) uint8_t ep1_[kPacketSize * 2];
    static constexpr size_t kEp1TxOffset = kPacketSize;
#ifdef UART_BRIDGE
    alignas(4) uint8_t ep5Notify_[8];
    alignas(4) uint8_t ep6Out_[kPacketSize];
    alignas(4) uint8_t ep7In_[kPacketSize];
#endif
    UartBridge* uart_ = nullptr;

    const uint8_t* descriptorCursor_ = nullptr;
    uint16_t setupLength_ = 0;
    uint8_t setupRequestType_ = 0;
    uint8_t setupRequest_ = 0;
    uint16_t setupValue_ = 0;
    uint16_t setupIndex_ = 0;

    volatile uint8_t pendingLength_ = 0;
    volatile bool packetPending_ = false;
    volatile bool packetTaken_ = false;
    volatile bool txBusy_ = false;
    volatile uint32_t txStartedMs_ = 0;
    volatile bool sessionResetPending_ = false;
#ifdef UART_BRIDGE
    volatile uint8_t cdcOutPendingLength_ = 0;
    volatile bool cdcOutPending_ = false;
    volatile bool cdcInBusy_ = false;
    volatile bool cdcInNeedsZlp_ = false;
    volatile bool cdcActivityPending_ = false;
    volatile bool uartRxDrainPending_ = false;
    volatile bool cdcStopPending_ = false;
    bool cdcLineCodingPending_ = false;
    volatile bool cdcSessionOpen_ = false;
    volatile bool controlOutStatusPending_ = false;
#endif
    volatile bool configured_ = false;
    uint8_t deviceAddress_ = 0;
    uint8_t deviceConfiguration_ = 0;
    volatile uint8_t sleepStatus_ = 0;

    static UsbWchLink* self_;
};
