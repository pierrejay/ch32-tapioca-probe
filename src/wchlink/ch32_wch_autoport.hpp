#pragma once

#include "ch32_sdk.hpp"
#include "dmi_interface.hpp"
#include "ch32_pioc_rvswio.hpp"
#include "ch32_pioc_rvswd.hpp"

// Auto-detecting WCH-Link transport. connect() probes RVSWD, then RVSWIO, and
// retains the first transport returning a valid DMSTATUS. PC19 carries data for
// both protocols; PC18 carries the RVSWD clock.
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
