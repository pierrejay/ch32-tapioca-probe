#pragma once

#include <stdint.h>

namespace WchLink
{

// Outcome of a DMI transaction, mapped to the WCH-Link reply status by the USB
// command decoder.
enum class DmiStatus : uint8_t
{
    Ok,
    Timeout,
    Parity,
    Busy,
    ProtocolFault,
};

// Hardware-independent WCH RISC-V Debug Module Interface port. The USB command
// decoder owns framing and session policy; this interface owns only the DMI
// register transaction over the physical transport (PIOC RVSWIO or RVSWD).
class IDmi
{
public:
    // Establish a debug session with the target. Returns false if no target
    // responds. Must be bounded - a missing target never wedges the caller.
    virtual bool connect() = 0;

    // One DMI register read/write. address is the 7-bit DMI address.
    virtual DmiStatus readDmi(uint8_t address, uint32_t& value) = 0;
    virtual DmiStatus writeDmi(uint8_t address, uint32_t value) = 0;

    // Release the session and park target pins as floating inputs.
    virtual void disconnect() = 0;

protected:
    ~IDmi() = default;
};

} // namespace WchLink
