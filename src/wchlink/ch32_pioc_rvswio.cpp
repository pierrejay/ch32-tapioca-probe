#include "ch32_pioc_rvswio.hpp"

#include "time.hpp"

extern "C" {
#include "PIOC_SFR.h"
#include <string.h>
}

using WchLink::DmiStatus;

namespace
{
constexpr uint8_t CtrlGo = 0x80;
constexpr uint8_t CtrlRead = 0x01;
constexpr uint8_t RwWrite = 0x01;
constexpr uint32_t EngineTimeoutIterations = 32000000;

// Mandatory inter-frame guard (cnlohr: 8 us; "2 us is sometimes too short", else
// back-to-back DMI ops merge). Measured from the previous frame so the wait overlaps
// the USB turnaround instead of sitting on the reply critical path -
// same wire spacing at ~0 latency, matching the RVSWD engine. -D RVSWIO_GUARD_US=n.
#ifndef RVSWIO_GUARD_US
#define RVSWIO_GUARD_US 8
#endif

void configureOutput(GPIO_TypeDef* port, uint32_t pin,
                     GPIOMode_TypeDef mode = GPIO_Mode_AF_PP)
{
    GPIO_InitTypeDef gpio = {};
    gpio.GPIO_Pin = pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = mode;
    GPIO_Init(port, &gpio);
}

void configureInput(GPIO_TypeDef* port, uint32_t pin)
{
    GPIO_InitTypeDef gpio = {};
    gpio.GPIO_Pin = pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(port, &gpio);
}
}

void Ch32PiocRvswio::init()
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_IO2W, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    // Park the wire as a floating input until a transaction is requested.
    configureInput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
}

void Ch32PiocRvswio::loadEngine()
{
    static const __attribute__((aligned(16))) unsigned char program[] =
        #include "../../pioc/tapioca_rvswio_inc.h"

    static_assert(sizeof(program) <= 4096, "PIOC program exceeds reserved SRAM");

    R8_SYS_CFG = 0;
    configureOutput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19, GPIO_Mode_AF_PP);
    memcpy(reinterpret_cast<void*>(PIOC_SRAM_BASE), program, sizeof(program));

    R8_DATA_REG0 = 0; // CTRL
    R8_DATA_REG1 = 0; // STATUS
    R8_DATA_REG2 = 0; // HEAD
    R32_DATA_REG4_7 = 0;

    R8_SYS_CFG = RB_MST_RESET;
    R8_SYS_CFG = RB_MST_IO_EN0 | RB_MST_IO_EN1;
    R8_SYS_CFG |= RB_MST_CLK_GATE;
    // Give the eMCU time to initialise its pads and reach WAIT_GO.
    Delay_Ms(1);
    engineLoaded_ = true;
}

bool Ch32PiocRvswio::runFrame(uint8_t command)
{
    // Honour the spacing from the previous frame while allowing USB turnaround to
    // consume it. Elapsed-time arithmetic also treats long-idle timestamps safely.
#if RVSWIO_GUARD_US > 0
    while ((uint32_t)(Time::micros() - lastFrameEndUs_) < RVSWIO_GUARD_US)
    {
        /* spin remainder */
    }
#endif

    R8_DATA_REG1 = 0; // STATUS = busy
    __asm volatile("" ::: "memory");
    R8_DATA_REG0 = static_cast<uint8_t>(command | CtrlGo);

    uint32_t timeout = EngineTimeoutIterations;
    // Observe GO consumed first (generation boundary), then STATUS complete.
    while ((R8_DATA_REG0 & CtrlGo) != 0 && timeout != 0) --timeout;
    while (R8_DATA_REG1 == 0 && timeout != 0) --timeout;
    if (timeout != 0)
    {
        // Record the frame end; reply on USB without blocking.
#if RVSWIO_GUARD_US > 0
        lastFrameEndUs_ = Time::micros();
#endif
        return true;
    }

    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    return false;
}

bool Ch32PiocRvswio::connect()
{
    if (!engineLoaded_) loadEngine();

    // WCH QingKe V2 debug init, matching minichlink's DefaultSetupInterface /
    // InitializeSWDSWIO: unlock slave output, make the debug module active, then
    // read DMCFGR back and require the 0x5aa5 signature. This doubles as a live
    // read+write proof of the RVSWIO wire, so a successful connect means the
    // whole transaction path works. Idempotent if the host also sends these.
    constexpr uint8_t DMCONTROL = 0x10;
    constexpr uint8_t DMCFGR = 0x7d;
    constexpr uint8_t DMSHDWCFGR = 0x7e;
    constexpr uint32_t kCfgUnlock = 0x5aa50000u | (1u << 10); // allow output from slave

    writeDmi(DMSHDWCFGR, kCfgUnlock);
    writeDmi(DMCFGR, kCfgUnlock);
    writeDmi(DMSHDWCFGR, kCfgUnlock); // once is sometimes not enough (per minichlink)
    writeDmi(DMCFGR, kCfgUnlock);
    writeDmi(DMCONTROL, 0x80000001);
    writeDmi(DMCONTROL, 0x80000001); // twice, as the proven references do

    uint32_t cfg = 0;
    if (readDmi(DMCFGR, cfg) != WchLink::DmiStatus::Ok) return false;
    return (cfg & 0xffff0000u) == 0x5aa50000u;
}

WchLink::DmiStatus Ch32PiocRvswio::writeDmi(uint8_t address, uint32_t value)
{
    if (!engineLoaded_) loadEngine();
    R8_DATA_REG2 = static_cast<uint8_t>((address << 1) | RwWrite);
    R32_DATA_REG4_7 = value;
    return runFrame(0x00) ? DmiStatus::Ok : DmiStatus::Timeout;
}

WchLink::DmiStatus Ch32PiocRvswio::readDmi(uint8_t address, uint32_t& value)
{
    if (!engineLoaded_) loadEngine();
    R8_DATA_REG2 = static_cast<uint8_t>((address << 1)); // rw = 0
    if (!runFrame(CtrlRead)) return DmiStatus::Timeout;
    value = R32_DATA_REG4_7;
    return DmiStatus::Ok;
}

void Ch32PiocRvswio::disconnect()
{
    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    configureInput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
}
