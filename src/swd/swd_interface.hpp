#pragma once

#include <stddef.h>
#include <stdint.h>

// Hardware-independent Serial Wire Debug port. CMSIS-DAP owns the ARM ADIv5
// transaction semantics; this interface owns only the wire-level signalling.
class ISwd
{
public:
    static constexpr uint8_t AckOk = 0x01;
    static constexpr uint8_t AckWait = 0x02;
    static constexpr uint8_t AckFault = 0x04;
    static constexpr uint8_t AckError = 0x08;

    virtual void init() = 0;
    virtual void activate() = 0;
    virtual void disconnect() = 0;
    virtual void setClockHz(uint32_t frequencyHz) = 0;
    virtual void setTurnaround(uint8_t cycles) = 0;
    virtual void setDataPhase(bool enabled) = 0;
    virtual void setIdleCycles(uint8_t cycles) = 0;

    // request: bit0 APnDP, bit1 RnW, bit2 A2, bit3 A3.
    virtual uint8_t transfer(uint8_t request, uint32_t* data) = 0;

    // CMSIS-DAP sequences are always packed least-significant bit first.
    virtual void writeSequence(uint16_t bitCount, const uint8_t* data) = 0;
    virtual void readSequence(uint16_t bitCount, uint8_t* data) = 0;

    virtual void writePins(uint8_t value, uint8_t select) = 0;
    virtual uint8_t readPins() const = 0;
    virtual bool resetTarget() = 0;

    // Optional implementation-specific diagnostics. CMSIS-DAP reserves
    // commands 0x80..0x9f for vendor use. Returning zero leaves the command
    // unsupported without coupling the portable CMSIS-DAP core to a backend.
    virtual size_t vendorCommand(const uint8_t*, size_t, uint8_t*, size_t) { return 0; }

protected:
    ~ISwd() = default;
};
