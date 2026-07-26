#pragma once

#include "ch32_sdk.hpp"
#include "swd_interface.hpp"

// Deterministic SWD physical layer built from Tapioca's validated PIOC timing
// primitives. PC18/IO0 is SWCLK and PC19/IO1 is SWDIO. The PIOC owns both pads
// for the complete connected session; the CPU only exchanges mailbox data.
class Ch32PiocSwd final : public ISwd
{
public:
    void init() override;
    void activate() override;
    void disconnect() override;
    void setClockHz(uint32_t frequencyHz) override;
    void setTurnaround(uint8_t cycles) override;
    void setDataPhase(bool enabled) override { dataPhase_ = enabled; }
    void setIdleCycles(uint8_t cycles) override { idleCycles_ = cycles; }
    uint8_t transfer(uint8_t request, uint32_t* data) override;
    void writeSequence(uint16_t bitCount, const uint8_t* data) override;
    void readSequence(uint16_t bitCount, uint8_t* data) override;
    void writePins(uint8_t value, uint8_t select) override;
    uint8_t readPins() const override;
    bool resetTarget() override;
    size_t vendorCommand(const uint8_t* request, size_t requestLength,
                         uint8_t* response, size_t responseCapacity) override;

private:
    bool runCommand(uint8_t command);
    void loadEngine();
    void ensureEngine();
    void configureOutput(GPIO_TypeDef* port, uint32_t pin,
                         GPIOMode_TypeDef mode = GPIO_Mode_Out_PP);
    void configureInput(GPIO_TypeDef* port, uint32_t pin);
    void setReset(bool high);
    bool getClock() const;
    bool getData() const;
    bool getReset() const;
    void recordAck(uint8_t ack);
    void resetStatistics();

    struct Statistics
    {
        uint32_t transfers = 0;
        uint32_t ok = 0;
        uint32_t wait = 0;
        uint32_t fault = 0;
        uint32_t error = 0;
        uint32_t mailboxTimeouts = 0;
    };

    uint32_t requestedClockHz_ = 1000000;
    uint8_t turnaroundCycles_ = 1;
    uint8_t idleCycles_ = 0;
    uint8_t pinState_ = 0x83; // SWCLK, SWDIO and nRESET deasserted.
    bool dataPhase_ = false;
    bool engineLoaded_ = false;
    Statistics statistics_ = {};
};
