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

enum class DmiOperation : uint8_t
{
    None,
    Read,
    Write,
};

enum class DmiTransport : uint8_t
{
    None,
    Rvswd,
    Rvswio,
};

// Latched transport evidence for post-mortem debugging. The first failed wire
// transaction is preserved while counters continue to accumulate, so cleanup
// traffic cannot overwrite the event that caused a host operation to abort.
struct DmiDiagnostics
{
    bool valid = false;
    DmiTransport transport = DmiTransport::None;
    DmiOperation operation = DmiOperation::None;
    uint8_t address = 0;
    DmiStatus status = DmiStatus::Ok;
    uint8_t rawStatus = 0;
    uint8_t receivedParity = 0xff;
    uint8_t expectedParity = 0xff;
    uint8_t rawLength = 0;
    uint8_t raw[5] = {};
    uint32_t data = 0;
    uint32_t wireFrames = 0;
    uint32_t busyReplies = 0;
    uint32_t targetFaults = 0;
    uint32_t parityErrors = 0;
    uint32_t engineTimeouts = 0;
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

    // Optional transport diagnostics. Implementations without raw target status
    // keep the defaults; querying diagnostics never initiates a target session.
    virtual bool getDiagnostics(DmiDiagnostics&) const { return false; }
    virtual void clearDiagnostics() {}

    // Release the session and park target pins as floating inputs.
    virtual void disconnect() = 0;

protected:
    ~IDmi() = default;
};

} // namespace WchLink
