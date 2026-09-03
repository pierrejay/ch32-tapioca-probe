#pragma once

#include "jtag_interface.hpp"
#include "ch32_sdk.hpp"

class Ch32Jtag final : public IJtag
{
public:
    void init() override;
    void activate() override;
    void disconnect() override;

    void setClockHz(uint32_t frequencyHz) override;
    void setTck(bool high) override;
    void setTdi(bool high) override;
    void setTms(bool high) override;
    bool getTdo() const override;
    void setTrst(bool high) override;
    void setSrst(bool high) override;
    void sequence(uint16_t bitCount, bool tms,
                  const uint8_t* input, uint8_t* output) override;
    bool clock(uint8_t pulses, bool tms, bool tdi) override;
    bool writePins(uint8_t value, uint8_t select) override;
    uint8_t readPins() const override;
    bool resetTarget() override;
    void delayUs(uint32_t microseconds) override;

private:
    void enterJtagMode();
    void halfPeriodDelay() const;
    bool pulseClock();
    void pulseClockNoCapture();
    void setTmsIfChanged(bool high);
    void setTdiIfChanged(bool high);

    // Calibrated approximately: one loop costs several core cycles.
    uint32_t halfPeriodLoops_ = 8;
    uint8_t pinState_ = 0xa0; // nTRST and nRESET released.
};
