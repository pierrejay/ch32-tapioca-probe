#pragma once

#include "jtag_interface.hpp"
#include "ch32_sdk.hpp"

class Ch32Jtag final : public IJtag
{
public:
    void init();
    void activate();
    void disconnect();

    void setFrequencyKhz(uint16_t frequencyKhz) override;
    void setTck(bool high) override;
    void setTdi(bool high) override;
    void setTms(bool high) override;
    bool getTdo() const override;
    void setTrst(bool high) override;
    void setSrst(bool high) override;
    void transfer(uint16_t bitCount, const uint8_t* in, uint8_t* out) override;
    bool clock(uint8_t pulses, bool tms, bool tdi) override;

private:
    void halfPeriodDelay() const;
    bool pulseClock();
    static void configureOutput(GPIO_TypeDef* port, uint32_t pin);
    static void configureInput(GPIO_TypeDef* port, uint32_t pin, GPIOMode_TypeDef mode);
    static void write(GPIO_TypeDef* port, uint32_t pin, bool high);
    static void setResetLine(GPIO_TypeDef* port, uint32_t pin, bool high);

    // Calibrated approximately: one loop costs several core cycles. Exact JTAG
    // frequency is deliberately not promised by DirtyJTAG implementations.
    uint32_t halfPeriodLoops_ = 8;
};
