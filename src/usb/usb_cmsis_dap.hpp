#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ch32_sdk.hpp"

class UartBridge;

// Interrupt-driven CH32X035 USBFS transport. Interface 0 carries CMSIS-DAP v2
// on EP1/EP2. A non-null UART pointer adds CDC ACM interfaces 1/2.
// SWD/JTAG work always runs in main context while EP1 OUT is NAKed.
class UsbCmsisDap
{
public:
    static constexpr size_t kPacketSize = 64;

    void init(UartBridge* uart = nullptr);
    bool configured() const { return configured_; }

    // CDC bridge orchestration lives here: main moves complete CDC OUT packets
    // into UART TX, while the system tick requests a UART RX drain in the USB
    // IRQ. Each completed CDC IN transfer immediately queues the next packet.
    // Returns true when the activity LED should be refreshed.
    bool pollCdc();
#ifdef UART_BRIDGE
    // Defers the actual UART RX read and CDC IN submission to USBFS_IRQHandler.
    void requestUartRxDrain();
#endif

    // EP1 remains NAKed until finish() completes the command.
    bool takeNextPacket(uint8_t* destination, size_t& length);

    // Reports a USB/session reset or an abandoned CMSIS-DAP reply.
    bool takeSessionReset();

    // Completes processing. A non-empty response is sent on EP2 IN; otherwise
    // EP1 OUT is immediately rearmed for the next command packet.
    bool finish(const uint8_t* response, size_t length);

    void onIrq();
    static void handleIrq() { if (self_) self_->onIrq(); }

private:
    void endpointInit();
    void resetCmsisDapEndpoints(bool resetOutToggle, bool resetInToggle);
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
    alignas(4) uint8_t ep1Out_[kPacketSize];
    alignas(4) uint8_t ep2In_[kPacketSize];
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

    static UsbCmsisDap* self_;
};
