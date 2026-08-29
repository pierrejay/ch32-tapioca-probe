#include "ch32_pioc_swd.hpp"
#include "pioc_swd_protocol.hpp"
#include "time.hpp"

extern "C" {
#include <string.h>
}

namespace
{
constexpr uint8_t PinSwclk = 0;
constexpr uint8_t PinSwdio = 1;
constexpr uint8_t PinNreset = 7;

constexpr uint8_t CtrlRead = 0x01;
constexpr uint8_t CtrlSequence = 0x02;
constexpr uint8_t CtrlPins = 0x04;
constexpr uint8_t CtrlIdle = 0x08;
constexpr uint8_t CtrlDataPhase = 0x10;
constexpr uint8_t CtrlGo = 0x80;

// A complete SWD command is far below 1 ms at the fixed 1 MHz wire clock.
// Use elapsed time rather than a compiler-dependent instruction-loop budget.
constexpr uint32_t EngineTimeoutUs = 5000;
constexpr uint8_t FixedHalfPeriodNops = 16;
constexpr uint32_t FixedClockHz = 1000000;

constexpr uint8_t VendorConfigure = 0x80;
constexpr uint8_t VendorStatistics = 0x81;
constexpr uint8_t VendorResetStatistics = 0x82;
constexpr uint8_t VendorStress = 0x83;

void write16(uint8_t* output, uint16_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8u);
}

void write32(uint8_t* output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8u);
    output[2] = static_cast<uint8_t>(value >> 16u);
    output[3] = static_cast<uint8_t>(value >> 24u);
}
}

void Ch32PiocSwd::configureOutput(GPIO_TypeDef* port, uint32_t pins,
                                  GPIO_CFGLR_PIN_MODE_Typedef mode)
{
    for (uint32_t bit = 0; bit < 24; ++bit)
    {
        if ((pins & (1u << bit)) == 0) continue;
        volatile uint32_t* cfg = bit < 8 ? &port->CFGLR :
                                 bit < 16 ? &port->CFGHR : &port->CFGXR;
        const uint32_t shift = (bit & 7u) * 4u;
        *cfg = (*cfg & ~(0xfu << shift)) | (static_cast<uint32_t>(mode) << shift);
    }
}

void Ch32PiocSwd::configureInput(GPIO_TypeDef* port, uint32_t pin)
{
    configureOutput(port, pin, GPIO_CFGLR_IN_FLOAT);
}

void Ch32PiocSwd::init()
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC |
                      RCC_APB2Periph_AFIO;
    RCC->AHBPCENR |= RCC_AHBPeriph_IO2W;
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_SWJ_CFG) | AFIO_PCFR1_SWJ_CFG_DISABLE;

    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    configureInput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
    setReset(true);
}

void Ch32PiocSwd::loadEngine()
{
    static const __attribute__((aligned(16))) unsigned char program[] =
        #include "../../pioc/tapioca_swd_inc.h"

    static_assert(sizeof(program) <= 4096, "PIOC program exceeds reserved SRAM");

    R8_SYS_CFG = 0;
    configureOutput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19, GPIO_CFGLR_OUT_10Mhz_AF_PP);
    memcpy(reinterpret_cast<void*>(PIOC_SRAM_BASE), program, sizeof(program));

    R8_DATA_REG0 = 0;
    R8_DATA_REG1 = 0;
    R8_DATA_REG2 = 0x03;
    R8_DATA_REG3 = 0;
    R32_DATA_REG4_7 = 0;
    R8_DATA_REG8 = 0;
    R8_DATA_REG9 = 0;
    R8_DATA_REG10 = turnaroundCycles_;
    R8_DATA_REG11 = idleCycles_;

    R8_SYS_CFG = RB_MST_RESET;
    R8_SYS_CFG = RB_MST_IO_EN0 | RB_MST_IO_EN1;
    R8_SYS_CFG |= RB_MST_CLK_GATE;
    // Give the eMCU time to initialize its pads and enter WAIT_GO.
    Delay_Ms(1);
    engineLoaded_ = true;
    pinState_ |= (1u << PinSwclk) | (1u << PinSwdio);
}

void Ch32PiocSwd::ensureEngine()
{
    if (!engineLoaded_) loadEngine();
}

bool Ch32PiocSwd::runCommand(uint8_t command)
{
    ensureEngine();

    R8_DATA_REG1 = 0;
    __asm volatile("" ::: "memory");
    R8_DATA_REG0 = static_cast<uint8_t>(command | CtrlGo);

    const uint32_t startedUs = Time::micros();
    // First observe GO being consumed. This is an explicit command/response
    // generation boundary and prevents a stale STATUS from satisfying a new
    // transaction even across the CPU/PIOC clock-domain synchronizer.
    while ((R8_DATA_REG0 & CtrlGo) != 0 &&
           (uint32_t)(Time::micros() - startedUs) < EngineTimeoutUs) {}
    while ((R8_DATA_REG0 & CtrlGo) == 0 && R8_DATA_REG1 == 0 &&
           (uint32_t)(Time::micros() - startedUs) < EngineTimeoutUs) {}
    if ((R8_DATA_REG0 & CtrlGo) == 0 && R8_DATA_REG1 != 0) return true;

    ++statistics_.mailboxTimeouts;
    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    return false;
}

void Ch32PiocSwd::activate()
{
    ensureEngine();
    setReset(true);
}

void Ch32PiocSwd::disconnect()
{
    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    configureInput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
    setReset(true);
    pinState_ |= (1u << PinSwclk) | (1u << PinSwdio) | (1u << PinNreset);
}

void Ch32PiocSwd::setClockHz(uint32_t frequencyHz)
{
    // Version 1 deliberately exposes one silicon-qualified timing profile.
    // Keep the CMSIS-DAP request for diagnostics without pretending that the
    // physical clock changed.
    requestedClockHz_ = frequencyHz ? frequencyHz : FixedClockHz;
}

void Ch32PiocSwd::setTurnaround(uint8_t cycles)
{
    if (cycles == 0) cycles = 1;
    if (cycles > 4) cycles = 4;
    turnaroundCycles_ = cycles;
    R8_DATA_REG10 = cycles;
}

uint8_t Ch32PiocSwd::transfer(uint8_t request, uint32_t* data)
{
    ensureEngine();
    const bool read = (request & 0x02u) != 0;

    R8_DATA_REG2 = PiocSwdProtocol::requestFrame(request);
    R8_DATA_REG10 = turnaroundCycles_;
    R8_DATA_REG11 = idleCycles_;
    if (!read)
    {
        const uint32_t value = data ? *data : 0;
        R32_DATA_REG4_7 = value;
        R8_DATA_REG8 = PiocSwdProtocol::parity32(value);
    }

    uint8_t command = read ? CtrlRead : 0;
    if (idleCycles_ != 0) command |= CtrlIdle;
    if (dataPhase_) command |= CtrlDataPhase;
    if (!runCommand(command))
    {
        recordAck(AckError);
        return AckError;
    }

    const uint8_t ack = static_cast<uint8_t>(R8_DATA_REG3 & 0x07u);
    if (!PiocSwdProtocol::validAck(ack))
    {
        recordAck(AckError);
        return AckError;
    }

    if (ack == AckOk && read)
    {
        const uint32_t value = R32_DATA_REG4_7;
        if ((R8_DATA_REG9 & 1u) != PiocSwdProtocol::parity32(value))
        {
            recordAck(AckError);
            return AckError;
        }
        if (data) *data = value;
    }

    recordAck(ack);
    return ack;
}

bool Ch32PiocSwd::writeSequence(uint16_t bitCount, const uint8_t* data)
{
    if (!data && bitCount != 0) return false;
    for (uint16_t bit = 0; bit < bitCount; ++bit)
    {
        R8_DATA_REG2 = static_cast<uint8_t>((data[bit >> 3u] >> (bit & 7u)) & 1u);
        if (!runCommand(CtrlSequence)) return false;
    }
    return true;
}

bool Ch32PiocSwd::readSequence(uint16_t bitCount, uint8_t* data)
{
    if (!data && bitCount != 0) return false;
    const size_t byteCount = (bitCount + 7u) / 8u;
    memset(data, 0, byteCount);
    for (uint16_t bit = 0; bit < bitCount; ++bit)
    {
        if (!runCommand(CtrlSequence | CtrlRead)) return false;
        if ((R8_DATA_REG9 & 1u) != 0) data[bit >> 3u] |= static_cast<uint8_t>(1u << (bit & 7u));
    }

    // Reclaim SWDIO as a driven-high output after the final sampled bit.
    R8_DATA_REG2 = 0x03;
    return runCommand(CtrlPins);
}

bool Ch32PiocSwd::writePins(uint8_t value, uint8_t select)
{
    pinState_ = static_cast<uint8_t>((pinState_ & ~select) | (value & select));
    bool success = true;
    if ((select & ((1u << PinSwclk) | (1u << PinSwdio))) != 0)
    {
        R8_DATA_REG2 = static_cast<uint8_t>(pinState_ & 0x03u);
        success = runCommand(CtrlPins);
    }
    if ((select & (1u << PinNreset)) != 0) setReset((pinState_ & (1u << PinNreset)) != 0);
    return success;
}

uint8_t Ch32PiocSwd::readPins() const
{
    uint8_t value = 0;
    if (getClock()) value |= 1u << PinSwclk;
    if (getData()) value |= 1u << PinSwdio;
    if (getReset()) value |= 1u << PinNreset;
    return value;
}

bool Ch32PiocSwd::resetTarget()
{
    setReset(false);
    Delay_Ms(10);
    setReset(true);
    return true;
}

void Ch32PiocSwd::delayUs(uint32_t microseconds)
{
    Delay_Us(microseconds);
}

void Ch32PiocSwd::setReset(bool high)
{
    // Open-drain emulation: never source the target reset rail.
    if (high) configureInput(GPIOB, GPIO_Pin_0);
    else
    {
        GPIOB->BCR = GPIO_Pin_0;
        configureOutput(GPIOB, GPIO_Pin_0);
        GPIOB->BCR = GPIO_Pin_0;
    }
}

bool Ch32PiocSwd::getClock() const
{
    return (GPIOC->INDR & GPIO_Pin_18) != 0;
}

bool Ch32PiocSwd::getData() const
{
    return (GPIOC->INDR & GPIO_Pin_19) != 0;
}

bool Ch32PiocSwd::getReset() const
{
    return (GPIOB->INDR & GPIO_Pin_0) != 0;
}

void Ch32PiocSwd::recordAck(uint8_t ack)
{
    ++statistics_.transfers;
    if (ack == AckOk) ++statistics_.ok;
    else if (ack == AckWait) ++statistics_.wait;
    else if (ack == AckFault) ++statistics_.fault;
    else ++statistics_.error;
}

void Ch32PiocSwd::resetStatistics()
{
    statistics_ = {};
}

size_t Ch32PiocSwd::vendorCommand(const uint8_t* request, size_t requestLength,
                                  uint8_t* response, size_t responseCapacity)
{
    if (!request || !response || requestLength == 0 || responseCapacity < 2) return 0;
    response[0] = request[0];

    switch (request[0])
    {
        case VendorConfigure:
            if (requestLength < 4 || responseCapacity < 5) return 0;
            response[1] = 0;
            response[2] = 0; // Continuous PIOC ownership; no parking mode.
            write16(response + 3, 0);
            return 5;

        case VendorStatistics:
            if (responseCapacity < 29) return 0;
            response[1] = 0;
            response[2] = 0;
            write16(response + 3, 0);
            write32(response + 5, statistics_.transfers);
            write32(response + 9, statistics_.ok);
            write32(response + 13, statistics_.wait);
            write32(response + 17, statistics_.fault);
            write32(response + 21, statistics_.error);
            write32(response + 25, statistics_.mailboxTimeouts);
            return 29;

        case VendorResetStatistics:
            resetStatistics();
            response[1] = 0;
            return 2;

        case VendorStress:
        {
            if (requestLength < 4 || responseCapacity < 15) return 0;
            const uint16_t count = static_cast<uint16_t>(request[1] |
                                                        (static_cast<uint16_t>(request[2]) << 8u));
            const uint8_t swdRequest = request[3] & 0x0fu;
            uint16_t completed = 0;
            uint8_t lastAck = AckError;
            uint32_t data = 0;
            if (requestLength >= 8)
                data = static_cast<uint32_t>(request[4]) |
                       (static_cast<uint32_t>(request[5]) << 8u) |
                       (static_cast<uint32_t>(request[6]) << 16u) |
                       (static_cast<uint32_t>(request[7]) << 24u);
            for (; completed < count; ++completed)
            {
                lastAck = transfer(swdRequest, &data);
                if (lastAck != AckOk) break;
            }
            if ((swdRequest & CtrlRead) == 0) data = R32_DATA_REG4_7;
            response[1] = lastAck;
            write16(response + 2, completed);
            write32(response + 4, data);
            write32(response + 8, statistics_.transfers);
            response[12] = FixedHalfPeriodNops;
            response[13] = R8_DATA_REG8 & 1u;
            response[14] = 0;
            return 15;
        }

        default:
            return 0;
    }
}
