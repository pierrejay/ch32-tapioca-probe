#pragma once

#include "ch32_sdk.hpp"
#include "dmi_interface.hpp"

// WCH RVSWD (two-wire) PIOC engine implementing the IDmi. One 52-bit RVSWD
// DMI transaction per PIOC mailbox round-trip: IO0/PC18 = SWCLK, IO1/PC19 = SWDIO
// (pioc/tapioca_rvswd.ASM). Framing/parity/packing live in the SDK-free codec
// (rvswd_frame.hpp); the blob is a fixed-geometry shift register. The host
// (minichlink) drives all DMI-level init and flash algorithms.
// See docs/wch-rvswd-protocol.md.
class Ch32PiocRvswd final : public WchLink::IDmi
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
    void latchError(WchLink::DmiOperation operation, uint8_t address,
                    uint32_t data, WchLink::DmiStatus status,
                    uint8_t rawStatus, const uint8_t* raw, uint8_t rawLength,
                    uint8_t receivedParity = 0xff,
                    uint8_t expectedParity = 0xff);

    bool engineLoaded_ = false;
    // The next frame waits only for the margin not consumed since this timestamp.
    uint32_t lastFrameEndUs_ = 0;
    WchLink::DmiDiagnostics diagnostics_;
};
