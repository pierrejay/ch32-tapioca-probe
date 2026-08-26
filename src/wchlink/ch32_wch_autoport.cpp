#include "ch32_wch_autoport.hpp"
#include "time.hpp"

using WchLink::DmiStatus;

namespace
{
constexpr uint8_t kDmStatus = 0x11; // RISC-V Debug Module status register
constexpr uint32_t kDmStatusAnyHalted = 1u << 8;
constexpr uint32_t kDmStatusAllHalted = 1u << 9;
constexpr uint32_t kHaltTimeoutUs = 5000;

// The transport setup issues haltreq. Accept it only after the single target hart
// reports halted; a readable Debug Module alone is not enough for abstract access.
bool wireResponds(WchLink::IDmi& port)
{
    if (!port.connect()) return false;

    const uint32_t startedUs = Time::micros();
    do
    {
        uint32_t status = 0;
        if (port.readDmi(kDmStatus, status) != DmiStatus::Ok) return false;

        const uint8_t version = static_cast<uint8_t>(status & 0x0f);
        if (version != 2 && version != 3) return false;

        constexpr uint32_t haltedMask = kDmStatusAnyHalted | kDmStatusAllHalted;
        if ((status & haltedMask) == haltedMask) return true;
    }
    while ((uint32_t)(Time::micros() - startedUs) < kHaltTimeoutUs);

    return false;
}
}

void Ch32WchAutoPort::init()
{
    // Both engines leave the shared pins as inputs.
    rvswio_.init();
    rvswd_.init();
    transport_ = Transport::None;
    lastTransport_ = Transport::None;
}

WchLink::IDmi* Ch32WchAutoPort::active()
{
    switch (transport_)
    {
        case Transport::Rvswd:  return &rvswd_;
        case Transport::Rvswio: return &rvswio_;
        default:                return nullptr;
    }
}

bool Ch32WchAutoPort::connect()
{
    transport_ = Transport::None;

    // Two-wire first, then one-wire; the transport whose DMSTATUS answers sanely wins.
    lastTransport_ = Transport::Rvswd;
    if (wireResponds(rvswd_))
    {
        transport_ = Transport::Rvswd;
        return true;
    }
    rvswd_.disconnect(); // release the wire before loading the other engine

    lastTransport_ = Transport::Rvswio;
    if (wireResponds(rvswio_))
    {
        transport_ = Transport::Rvswio;
        return true;
    }
    rvswio_.disconnect();
    return false;
}

void Ch32WchAutoPort::ensureDetected()
{
    if (transport_ == Transport::None) connect();
}

DmiStatus Ch32WchAutoPort::readDmi(uint8_t address, uint32_t& value)
{
    ensureDetected();
    WchLink::IDmi* port = active();
    if (port == nullptr) return DmiStatus::ProtocolFault;
    return port->readDmi(address, value);
}

DmiStatus Ch32WchAutoPort::writeDmi(uint8_t address, uint32_t value)
{
    ensureDetected();
    WchLink::IDmi* port = active();
    if (port == nullptr) return DmiStatus::ProtocolFault;
    return port->writeDmi(address, value);
}

bool Ch32WchAutoPort::getDiagnostics(WchLink::DmiDiagnostics& diagnostics) const
{
    switch (lastTransport_)
    {
        case Transport::Rvswd: return rvswd_.getDiagnostics(diagnostics);
        case Transport::Rvswio: return rvswio_.getDiagnostics(diagnostics);
        default: return false;
    }
}

void Ch32WchAutoPort::clearDiagnostics()
{
    rvswd_.clearDiagnostics();
    rvswio_.clearDiagnostics();
}

void Ch32WchAutoPort::disconnect()
{
    if (WchLink::IDmi* port = active()) port->disconnect();
    transport_ = Transport::None;
}
