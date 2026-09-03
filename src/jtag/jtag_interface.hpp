#pragma once

#include <stdint.h>

// Hardware-independent JTAG signal interface.
// Signal arguments use electrical levels: true = high/released, false = low.
class IJtag
{
public:
    virtual void init() = 0;
    virtual void activate() = 0;
    virtual void disconnect() = 0;
    virtual void setClockHz(uint32_t frequencyHz) = 0;
    virtual void setTck(bool high) = 0;
    virtual void setTdi(bool high) = 0;
    virtual void setTms(bool high) = 0;
    virtual bool getTdo() const = 0;
    virtual void setTrst(bool high) = 0;
    virtual void setSrst(bool high) = 0;

    // CMSIS-DAP sequences are packed least-significant bit first. TMS remains
    // fixed for the complete sequence; output may be null when TDO is ignored.
    virtual void sequence(uint16_t bitCount, bool tms,
                          const uint8_t* input, uint8_t* output) = 0;

    // Clock with fixed TMS/TDI levels and return the final sampled TDO level.
    virtual bool clock(uint8_t pulses, bool tms, bool tdi) = 0;

    virtual bool writePins(uint8_t value, uint8_t select) = 0;
    virtual uint8_t readPins() const = 0;
    virtual bool resetTarget() = 0;
    virtual void delayUs(uint32_t microseconds) = 0;

protected:
    ~IJtag() = default;
};
