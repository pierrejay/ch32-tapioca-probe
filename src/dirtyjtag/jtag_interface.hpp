#pragma once

#include <stdint.h>

// Hardware-independent interface consumed by the DirtyJTAG command core.
// Signal arguments use electrical levels: true = high/released, false = low.
class IJtag
{
public:
    virtual void setFrequencyKhz(uint16_t frequencyKhz) = 0;
    virtual void setTck(bool high) = 0;
    virtual void setTdi(bool high) = 0;
    virtual void setTms(bool high) = 0;
    virtual bool getTdo() const = 0;
    virtual void setTrst(bool high) = 0;
    virtual void setSrst(bool high) = 0;

    // Bits are shifted MSB-first within each byte. out may be null for a
    // write-only transfer. TMS remains low for the entire transfer.
    virtual void transfer(uint16_t bitCount, const uint8_t* in, uint8_t* out) = 0;

    // Clock with fixed TMS/TDI levels and return the final sampled TDO level.
    virtual bool clock(uint8_t pulses, bool tms, bool tdi) = 0;

protected:
    ~IJtag() = default;
};
