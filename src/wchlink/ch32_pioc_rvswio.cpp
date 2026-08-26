#include "ch32_pioc_rvswio.hpp"

#include "time.hpp"

extern "C" {
#include <string.h>
}

using WchLink::DmiStatus;

namespace
{
constexpr uint8_t CtrlGo = 0x80;
constexpr uint8_t CtrlRead = 0x01;
constexpr uint8_t RwWrite = 0x01;
// A wire frame completes in well under 1 ms; this leaves ample target margin
// while keeping a failed engine far below the host's 5 s command timeout.
constexpr uint32_t EngineTimeoutUs = 5000;

// Mandatory inter-frame guard (cnlohr: 8 us; "2 us is sometimes too short", else
// back-to-back DMI ops merge). Measured from the previous frame so the wait overlaps
// the USB turnaround instead of sitting on the reply critical path -
// same wire spacing at ~0 latency, matching the RVSWD engine. -D RVSWIO_GUARD_US=n.
#ifndef RVSWIO_GUARD_US
#define RVSWIO_GUARD_US 8
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
}

void Ch32PiocRvswio::init()
{
    clearDiagnostics();
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
    RCC->AHBPCENR |= RCC_AHBPeriph_IO2W;
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_SWJ_CFG) | AFIO_PCFR1_SWJ_CFG_DISABLE;

    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    // Park the wire as a floating input until a transaction is requested.
    configureInput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
}

bool Ch32PiocRvswio::getDiagnostics(WchLink::DmiDiagnostics& diagnostics) const
{
    diagnostics = diagnostics_;
    return true;
}

void Ch32PiocRvswio::clearDiagnostics()
{
    diagnostics_ = {};
    diagnostics_.transport = WchLink::DmiTransport::Rvswio;
}

void Ch32PiocRvswio::latchTimeout(WchLink::DmiOperation operation,
                                  uint8_t address, uint32_t data)
{
    ++diagnostics_.engineTimeouts;
    if (diagnostics_.valid) return;
    diagnostics_.valid = true;
    diagnostics_.operation = operation;
    diagnostics_.address = address;
    diagnostics_.status = DmiStatus::Timeout;
    diagnostics_.rawStatus = 0xff;
    diagnostics_.data = data;
}

void Ch32PiocRvswio::loadEngine()
{
    static const __attribute__((aligned(16))) unsigned char program[] =
        #include "../../pioc/tapioca_rvswio_inc.h"

    static_assert(sizeof(program) <= 4096, "PIOC program exceeds reserved SRAM");

    R8_SYS_CFG = 0;
    configureOutput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
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

    const uint32_t startedUs = Time::micros();
    // Observe GO consumed first (generation boundary), then STATUS complete.
    while ((R8_DATA_REG0 & CtrlGo) != 0 &&
           (uint32_t)(Time::micros() - startedUs) < EngineTimeoutUs) {}
    while ((R8_DATA_REG0 & CtrlGo) == 0 && R8_DATA_REG1 == 0 &&
           (uint32_t)(Time::micros() - startedUs) < EngineTimeoutUs) {}
    if ((R8_DATA_REG0 & CtrlGo) == 0 && R8_DATA_REG1 != 0)
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
    clearDiagnostics();
    if (!engineLoaded_) loadEngine();

    // WCH QingKe V2 debug init, matching minichlink's DefaultSetupInterface /
    // InitializeSWDSWIO: unlock slave output, make the debug module active, then
    // read DMCFGR back as a live read+write transaction. Its value is not portable;
    // the auto-port validates DMSTATUS instead. Idempotent if the host repeats it.
    constexpr uint8_t DMCONTROL = 0x10;
    constexpr uint8_t DMCFGR = 0x7d;
    constexpr uint8_t DMSHDWCFGR = 0x7e;
    constexpr uint32_t kCfgUnlock = 0x5aa50000u | (1u << 10); // allow output from slave

    // Protocol-level failures may recover on the repeated init writes. An engine
    // timeout cannot, so do not multiply it across the rest of the sequence.
    if (writeDmi(DMSHDWCFGR, kCfgUnlock) == DmiStatus::Timeout) return false;
    if (writeDmi(DMCFGR, kCfgUnlock) == DmiStatus::Timeout) return false;
    if (writeDmi(DMSHDWCFGR, kCfgUnlock) == DmiStatus::Timeout) return false;
    if (writeDmi(DMCFGR, kCfgUnlock) == DmiStatus::Timeout) return false;
    if (writeDmi(DMCONTROL, 0x80000001) == DmiStatus::Timeout) return false;
    if (writeDmi(DMCONTROL, 0x80000001) == DmiStatus::Timeout) return false;

    uint32_t cfg = 0;
    if (readDmi(DMCFGR, cfg) != WchLink::DmiStatus::Ok) return false;
    return true;
}

WchLink::DmiStatus Ch32PiocRvswio::writeDmi(uint8_t address, uint32_t value)
{
    if (!engineLoaded_) loadEngine();
    R8_DATA_REG2 = static_cast<uint8_t>((address << 1) | RwWrite);
    R32_DATA_REG4_7 = value;
    ++diagnostics_.wireFrames;
    if (runFrame(0x00)) return DmiStatus::Ok;
    latchTimeout(WchLink::DmiOperation::Write, address, value);
    return DmiStatus::Timeout;
}

WchLink::DmiStatus Ch32PiocRvswio::readDmi(uint8_t address, uint32_t& value)
{
    if (!engineLoaded_) loadEngine();
    R8_DATA_REG2 = static_cast<uint8_t>((address << 1)); // rw = 0
    ++diagnostics_.wireFrames;
    if (!runFrame(CtrlRead))
    {
        latchTimeout(WchLink::DmiOperation::Read, address, 0);
        return DmiStatus::Timeout;
    }
    value = R32_DATA_REG4_7;
    return DmiStatus::Ok;
}

void Ch32PiocRvswio::disconnect()
{
    R8_SYS_CFG = 0;
    engineLoaded_ = false;
    configureInput(GPIOC, GPIO_Pin_18 | GPIO_Pin_19);
}
