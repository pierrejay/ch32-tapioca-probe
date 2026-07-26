#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ch32_sdk.hpp"

// Interrupt-driven CH32X035 USBFS transport for the WCH-Link personality.
// A single vendor interface 0 with a bidirectional bulk endpoint 1: OUT 0x01 and
// IN 0x81 share one DMA buffer (WCH single-buffer mode). Command decoding runs in
// main context while EP1 OUT is NAKed; the reply is sent on EP1 IN.
class UsbWchLink
{
public:
    static constexpr size_t kPacketSize = 64;

    void init();
    bool configured() const { return configured_; }

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
    void handleSetup();
    void handleEp0In();
    void busReset();
    void armOut();

    alignas(4) uint8_t ep0_[kPacketSize];
    // EP1 bidirectional buffer. With RX_EN|TX_EN and BUF_MOD=0 the CH32X035
    // USBFS lays out 128 bytes at UEP1_DMA: OUT (RX) in [0..63], IN (TX) in
    // [64..127] (ch32x035_usb.h endpoint-mode table). TX must be written at +64.
    alignas(4) uint8_t ep1_[kPacketSize * 2];
    static constexpr size_t kEp1TxOffset = kPacketSize;

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
    volatile bool sessionResetPending_ = false;
    volatile bool configured_ = false;
    uint8_t deviceAddress_ = 0;
    uint8_t deviceConfiguration_ = 0;
    uint8_t sleepStatus_ = 0;

    static UsbWchLink* self_;
};
