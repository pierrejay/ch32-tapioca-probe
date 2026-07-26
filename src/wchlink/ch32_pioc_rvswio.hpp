#pragma once

#include "ch32_sdk.hpp"
#include "dmi_interface.hpp"

// WCH RVSWIO (one-wire) PIOC engine implementing the IDmi. One contiguous
// DMI transaction per PIOC mailbox round-trip on IO1/PC19 (pioc/tapioca_rvswio.ASM).
// The host (minichlink) drives all DMI-level init and flash algorithms; this
// engine only executes single read/write transactions. See docs/wch-rvswio-protocol.md.
class Ch32PiocRvswio final : public WchLink::IDmi
{
public:
    void init();

    // IDmi. connect() is minimal: the host performs the DMI init sequence
    // via DMI writes, so this only ensures the PIOC engine is live.
    bool connect() override;
    WchLink::DmiStatus readDmi(uint8_t address, uint32_t& value) override;
    WchLink::DmiStatus writeDmi(uint8_t address, uint32_t value) override;
    void disconnect() override;

private:
    void loadEngine();
    bool runFrame(uint8_t command);

    bool engineLoaded_ = false;
    // Deferred inter-frame guard: end-of-frame stamps this deadline; the next frame
    // waits only for whatever margin the USB turnaround did not already consume.
    uint32_t guardDeadlineUs_ = 0;
};
