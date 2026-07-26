#pragma once

#include "ch32_sdk.hpp"
#include "dmi_interface.hpp"
#include "ch32_pioc_rvswio.hpp"
#include "ch32_pioc_rvswd.hpp"

// Unified auto-detecting WCH-Link transport (the shipping product). Carries BOTH
// the one-wire RVSWIO and two-wire RVSWD PIOC engines and selects whichever the
// target answers on - like a real WCH-LinkE, with no rebuild/reflash per target.
//
// connect() tries RVSWD (two-wire) first, then RVSWIO (one-wire). Each engine's
// own connect() runs the WCH unlock and requires DMCFGR to read back 0x5aa5, so it
// doubles as a "does the target answer on this transport?" probe. The winner is
// cached for the session; readDmi/writeDmi delegate to it. A later reconnect
// re-detects, so swapping a CH32V003 for a CH32V307 just works.
//
// Shared pinout: PC19 = DATA (SWIO and SWDIO), PC18 = CLK (RVSWD only). One blob
// runs at a time; connect() loads each in turn. See docs/wch-rvswd-protocol.md and
// docs/wch-rvswio-protocol.md for the two wire transports.
class Ch32WchAutoPort final : public WchLink::IDmi
{
public:
    void init();

    bool connect() override;
    WchLink::DmiStatus readDmi(uint8_t address, uint32_t& value) override;
    WchLink::DmiStatus writeDmi(uint8_t address, uint32_t value) override;
    void disconnect() override;

private:
    enum class Transport : uint8_t { None, Rvswd, Rvswio };

    WchLink::IDmi* active();
    // Auto-detect on first use if the host issued DMI ops without a Connect command.
    void ensureDetected();

    Ch32PiocRvswio rvswio_;
    Ch32PiocRvswd rvswd_;
    Transport transport_ = Transport::None;
};
