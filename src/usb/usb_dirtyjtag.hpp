#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ch32_sdk.hpp"

// Interrupt-driven CH32X035 USBFS composite transport. Interface 0 carries
// DirtyJTAG on EP1/EP2; interface 1 carries CMSIS-DAP v2 on EP4/EP3.
// Wire work always runs in main context while the corresponding OUT endpoint
// is NAKed.
class UsbDirtyJtag
{
public:
    static constexpr size_t kPacketSize = 64;

    void init();
    bool configured() const { return configured_; }

    // Copies the oldest pending OUT packet across both interfaces. This keeps
    // first-arrival ownership independent of main-loop polling order. The
    // selected endpoint remains NAKed until its matching finish method.
    bool takeNextPacket(uint8_t* destination, size_t& length, bool& cmsisDap);

    // Reports which transport session must release the shared target wire.
    bool takeSessionReset(bool& dirtyJtag, bool& cmsisDap);

    // Completes processing. A non-empty response is sent on EP2 IN; otherwise
    // EP1 OUT is immediately rearmed for the next command packet.
    bool finish(const uint8_t* response, size_t length);

    bool finishCmsisDap(const uint8_t* response, size_t length);

    void onIrq();
    static void handleIrq() { if (self_) self_->onIrq(); }

private:
    void endpointInit();
    void resetDirtyJtagEndpoints(bool resetOutToggle, bool resetInToggle);
    void resetCmsisDapEndpoints(bool resetOutToggle, bool resetInToggle);
    void handleSetup();
    void handleEp0In();
    void busReset();
    void armOut();
    void armCmsisDapOut();

    // CH32X035 EP4 has no DMA register: its fixed buffer starts 64 bytes
    // after UEP0_DMA, hence this deliberately contiguous allocation.
    alignas(4) uint8_t ep0_[kPacketSize * 2];
    alignas(4) uint8_t ep1Out_[kPacketSize];
    alignas(4) uint8_t ep2In_[kPacketSize];
    alignas(4) uint8_t ep3In_[kPacketSize];

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
    volatile uint8_t dapPendingLength_ = 0;
    volatile bool dapPacketPending_ = false;
    volatile bool dapPacketTaken_ = false;
    volatile bool dapTxBusy_ = false;
    volatile uint32_t dapTxStartedMs_ = 0;
    volatile uint32_t arrivalCounter_ = 0;
    volatile uint32_t dirtyJtagArrival_ = 0;
    volatile uint32_t cmsisDapArrival_ = 0;
    volatile bool dirtyJtagResetPending_ = false;
    volatile bool cmsisDapResetPending_ = false;
    volatile bool configured_ = false;
    uint8_t deviceAddress_ = 0;
    uint8_t deviceConfiguration_ = 0;
    uint8_t sleepStatus_ = 0;

    static UsbDirtyJtag* self_;
};
