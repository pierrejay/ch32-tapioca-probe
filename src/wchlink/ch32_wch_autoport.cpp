#include "ch32_wch_autoport.hpp"

using WchLink::DmiStatus;

namespace
{
constexpr uint8_t kDmStatus = 0x11; // RISC-V Debug Module status register

// Does a target answer on this transport? Best-effort init (the engine's connect()
// runs the WCH unlock + sets dmactive), then read DMSTATUS and require a valid
// RISC-V debug version field (2 = 0.13, 3 = 1.0) with valid parity. This is a
// stronger, unlock-independent signal than the per-engine 0x5aa5 check: a live wire
// answers with a sane DMSTATUS; the wrong wire floats (version 0xf / parity fault).
bool wireResponds(WchLink::IDmi& port)
{
    port.connect(); // best-effort; ignore its strict 0x5aa5 verdict
    uint32_t status = 0;
    if (port.readDmi(kDmStatus, status) != DmiStatus::Ok) return false;
    const uint8_t version = static_cast<uint8_t>(status & 0x0f);
    return version == 2 || version == 3;
}
}

void Ch32WchAutoPort::init()
{
    // Both engines share the same RCC/GPIO bring-up and park PC18/PC19 as inputs;
    // initialising both leaves either ready to be loaded by connect().
    rvswio_.init();
    rvswd_.init();
    transport_ = Transport::None;
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
    if (wireResponds(rvswd_))
    {
        transport_ = Transport::Rvswd;
        return true;
    }
    rvswd_.disconnect(); // release the wire before loading the other engine

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
    // connect() delegates to the concrete engines' connect()/writeDmi, never back to
    // this port, so there is no recursion. A failed probe leaves transport_ == None.
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

void Ch32WchAutoPort::disconnect()
{
    if (WchLink::IDmi* port = active()) port->disconnect();
    transport_ = Transport::None;
}
