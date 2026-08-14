#include "ch32_pioc_rvswd.hpp"

#include "rvswd_frame.hpp"
#include "time.hpp"

extern "C" {
#include <string.h>
}

using WchLink::DmiStatus;
namespace R = WchLink::Rvswd;

namespace
{
constexpr uint8_t CtrlGo = 0x80;
constexpr uint8_t CtrlRead = 0x01;
constexpr uint32_t EngineTimeoutIterations = 32000000;

// Inter-frame guard, in microseconds: minimum spacing between the end of one RVSWD
// frame and the START of the next, on the wire. Measured from the previous frame so
// the wait overlaps the USB turnaround instead of sitting on the reply
// critical path - we keep the full 8 us wire margin at ~zero latency cost.
// Overridable at build time (-D RVSWD_GUARD_US=n).
#ifndef RVSWD_GUARD_US
#define RVSWD_GUARD_US 8
#endif

void configureOutput(GPIO_TypeDef* port, uint32_t pin,
                     GPIO_CFGLR_PIN_MODE_Typedef mode = GPIO_CFGLR_OUT_10Mhz_AF_PP)
{
    for (uint32_t bit = 0; bit < 24; ++bit)
    {
        if ((pin & (1u << bit)) == 0) continue;
        volatile uint32_t* cfg = bit < 8 ? &port->CFGLR :
                                 bit < 16 ? &port->CFGHR : &port->CFGXR;
        const uint32_t shift = (bit & 7u) * 4u;
        *cfg = (*cfg & ~(0xfu << shift)) | (static_cast<uint32_t>(mode) << shift);
    }
}

void configureInput(GPIO_TypeDef* port, uint32_t pin)
{
    configureOutput(port, pin, GPIO_CFGLR_IN_FLOAT);
}

// Map the raw 2-bit target status onto the transport-level DmiStatus.
DmiStatus mapStatus(uint8_t raw)
{
    switch (raw)
    {
    case R::kStatusOk:
    case R::kStatusOkAlt:
        return DmiStatus::Ok;
    case R::kStatusBusy:
        return DmiStatus::Busy;
    default:
        return DmiStatus::ProtocolFault; // kStatusFail (2)
    }
}
}

void Ch32PiocRvswd::init()
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
    RCC->AHBPCENR |= RCC_AHBPeriph_IO2W;
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_SWJ_CFG) | AFIO_PCFR1_SWJ_CFG_DISABLE;

    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    // Park both wires as floating inputs until a transaction is requested.
    configureInput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
}

void Ch32PiocRvswd::loadEngine()
{
    static const __attribute__((aligned(16))) unsigned char program[] =
        #include "../../pioc/tapioca_rvswd_inc.h"

    static_assert(sizeof(program) <= 4096, "PIOC program exceeds reserved SRAM");

    R8_SYS_CFG = 0;
    configureOutput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
    memcpy(reinterpret_cast<void*>(PIOC_SRAM_BASE), program, sizeof(program));

    R8_DATA_REG0 = 0; // CTRL
    R8_DATA_REG1 = 0; // STATUS
    R32_DATA_REG4_7 = 0;
    R32_DATA_REG8_11 = 0;
    R32_DATA_REG12_15 = 0;

    R8_SYS_CFG = RB_MST_RESET;
    R8_SYS_CFG = RB_MST_IO_EN0 | RB_MST_IO_EN1;
    R8_SYS_CFG |= RB_MST_CLK_GATE;
    Delay_Ms(1); // let the eMCU reach WAIT_GO
    engineLoaded_ = true;
}

bool Ch32PiocRvswd::runFrame(uint8_t command)
{
    // Honour the spacing from the previous frame while allowing USB turnaround to
    // consume it. Elapsed-time arithmetic also treats long-idle timestamps safely.
#if RVSWD_GUARD_US > 0
    while ((uint32_t)(Time::micros() - lastFrameEndUs_) < RVSWD_GUARD_US)
    {
        /* spin remainder */
    }
#endif

    R8_DATA_REG1 = 0; // STATUS = busy
    __asm volatile("" ::: "memory");
    R8_DATA_REG0 = static_cast<uint8_t>(command | CtrlGo);

    uint32_t timeout = EngineTimeoutIterations;
    // Generation boundary: observe GO consumed, then STATUS complete.
    while ((R8_DATA_REG0 & CtrlGo) != 0 && timeout != 0) --timeout;
    while (R8_DATA_REG1 == 0 && timeout != 0) --timeout;
    if (timeout != 0)
    {
        // Record the frame end; reply on USB without blocking.
#if RVSWD_GUARD_US > 0
        lastFrameEndUs_ = Time::micros();
#endif
        return true;
    }

    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    return false;
}

bool Ch32PiocRvswd::connect()
{
    if (!engineLoaded_) loadEngine();

    // WCH QingKe debug init (same DMI-level sequence as RVSWIO/minichlink): unlock
    // slave output, make the debug module active, then read DMCFGR back and require
    // the 0x5aa5 signature. Proves the two-wire read+write path end to end.
    constexpr uint8_t DMCONTROL = 0x10;
    constexpr uint8_t DMCFGR = 0x7d;
    constexpr uint8_t DMSHDWCFGR = 0x7e;
    constexpr uint32_t kCfgUnlock = 0x5aa50000u | (1u << 10);

    writeDmi(DMSHDWCFGR, kCfgUnlock);
    writeDmi(DMCFGR, kCfgUnlock);
    writeDmi(DMSHDWCFGR, kCfgUnlock);
    writeDmi(DMCFGR, kCfgUnlock);
    writeDmi(DMCONTROL, 0x80000001);
    writeDmi(DMCONTROL, 0x80000001);

    uint32_t cfg = 0;
    if (readDmi(DMCFGR, cfg) != DmiStatus::Ok) return false;
    return (cfg & 0xffff0000u) == 0x5aa50000u;
}

WchLink::DmiStatus Ch32PiocRvswd::writeDmi(uint8_t address, uint32_t value)
{
    if (!engineLoaded_) loadEngine();

    R::HostFrame f = R::packWrite(address, value);
    R8_DATA_REG4 = f.bytes[0];
    R8_DATA_REG5 = f.bytes[1];
    R8_DATA_REG6 = f.bytes[2];
    R8_DATA_REG7 = f.bytes[3];
    R8_DATA_REG8 = f.bytes[4];
    R8_DATA_REG9 = f.bytes[5];

    if (!runFrame(0x00)) return DmiStatus::Timeout; // CTRL bit0 clear = write

    uint8_t targ[R::kFrameBytes] = {};
    targ[0] = R8_DATA_REG11;
    return mapStatus(R::unpackWriteStatus(targ));
}

WchLink::DmiStatus Ch32PiocRvswd::readDmi(uint8_t address, uint32_t& value)
{
    if (!engineLoaded_) loadEngine();

    R::HostFrame f = R::packRead(address);
    R8_DATA_REG4 = f.bytes[0];
    R8_DATA_REG5 = f.bytes[1];

    if (!runFrame(CtrlRead)) return DmiStatus::Timeout;

    uint8_t targ[R::kFrameBytes] = {};
    targ[0] = R8_DATA_REG11;
    targ[1] = R8_DATA_REG12;
    targ[2] = R8_DATA_REG13;
    targ[3] = R8_DATA_REG14;
    targ[4] = R8_DATA_REG15;

    R::ReadReply rep = R::unpackRead(targ);
    value = rep.data;
    if (!rep.parityOk) return DmiStatus::Parity;
    return mapStatus(rep.status);
}

void Ch32PiocRvswd::disconnect()
{
    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    configureInput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
}
