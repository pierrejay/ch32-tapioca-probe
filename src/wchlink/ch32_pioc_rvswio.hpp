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

    // Performs the WCH QingKe debug setup and an end-to-end DMI read. The
    // returned configuration value is not portable; the auto-port validates
    // DMSTATUS and the chip identity separately.
    bool connect() override;
    WchLink::DmiStatus readDmi(uint8_t address, uint32_t& value) override;
    WchLink::DmiStatus writeDmi(uint8_t address, uint32_t value) override;
    bool getDiagnostics(WchLink::DmiDiagnostics& diagnostics) const override;
    void clearDiagnostics() override;
    void disconnect() override;

private:
    void loadEngine();
    bool runFrame(uint8_t command);
    void latchTimeout(WchLink::DmiOperation operation, uint8_t address,
                      uint32_t data);

    bool engineLoaded_ = false;
    // The next frame waits only for the margin not consumed since this timestamp.
    uint32_t lastFrameEndUs_ = 0;
    WchLink::DmiDiagnostics diagnostics_;
};
