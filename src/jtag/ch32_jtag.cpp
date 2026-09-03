#include "ch32_jtag.hpp"
#include "board_config.hpp"
#include "hal/ch32_gpio.hpp"

void Ch32Jtag::init()
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                      RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
    // TCK/TMS are on PC18/PC19, the chip's SDI debug pins - free them for GPIO
    // use (idempotent; the SWD backend does the same for PIOC).
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_SWJ_CFG) | AFIO_PCFR1_SWJ_CFG_DISABLE;

    setClockHz(JTAG_DEFAULT_FREQUENCY_KHZ * 1000u);
    disconnect();
}

void Ch32Jtag::activate()
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                      RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
    // TCK/TMS are on PC18/PC19, the chip's SDI debug pins - free them for GPIO
    // use (idempotent; the SWD backend does the same for PIOC).
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_SWJ_CFG) | AFIO_PCFR1_SWJ_CFG_DISABLE;

    Ch32Gpio::configure(JTAG_TCK_PORT, JTAG_TCK_PIN, GPIO_CFGLR_OUT_10Mhz_PP);
    Ch32Gpio::configure(JTAG_TDI_PORT, JTAG_TDI_PIN, GPIO_CFGLR_OUT_10Mhz_PP);
    Ch32Gpio::configure(JTAG_TMS_PORT, JTAG_TMS_PIN, GPIO_CFGLR_OUT_10Mhz_PP);
    JTAG_TDO_PORT->BCR = JTAG_TDO_PIN;
    Ch32Gpio::configure(JTAG_TDO_PORT, JTAG_TDO_PIN, GPIO_CFGLR_IN_PUPD);

    setTck(false);
    setTdi(false);
    setTms(false);
    setSrst(true);
    setTrst(true);

    enterJtagMode();
}

void Ch32Jtag::enterJtagMode()
{
    // Some hosts only request DAP_Connect(JTAG) and do not switch a target
    // previously left in SWD. Emit the standard ARM SWD-to-JTAG sequence so a
    // connection recovers from either normal ARM wire mode. On a SWJ-DP already
    // in JTAG it ends with the TAP in reset; a target left in the dormant state
    // still requires a host wake-up.
    setTdiIfChanged(true);
    setTmsIfChanged(true);
    if (pinState_ & 0x01u) setTck(false);

    for (uint8_t bit = 0; bit < 56; ++bit) pulseClockNoCapture();

    uint16_t switchCode = 0xe73cu;
    for (uint8_t bit = 0; bit < 16; ++bit)
    {
        setTmsIfChanged((switchCode & 1u) != 0);
        pulseClockNoCapture();
        switchCode >>= 1u;
    }

    setTmsIfChanged(true);
    for (uint8_t bit = 0; bit < 8; ++bit) pulseClockNoCapture();
}

void Ch32Jtag::disconnect()
{
    Ch32Gpio::floatPins(JTAG_TCK_PORT, JTAG_TCK_PIN);
    Ch32Gpio::floatPins(JTAG_TDI_PORT, JTAG_TDI_PIN);
    Ch32Gpio::floatPins(JTAG_TMS_PORT, JTAG_TMS_PIN);
    Ch32Gpio::floatPins(JTAG_TDO_PORT, JTAG_TDO_PIN);
    setSrst(true);
#ifdef JTAG_TRST
    setTrst(true);
#endif
}

void Ch32Jtag::setClockHz(uint32_t frequencyHz)
{
    if (frequencyHz < 1000u) frequencyHz = 1000u;
    if (frequencyHz > 2000000u) frequencyHz = 2000000u;

    // The register loop is two instructions per iteration. GPIO accesses add
    // further overhead, so the resulting clock never exceeds the request.
    const uint32_t cyclesPerHalfPeriod =
        FUNCONF_SYSTEM_CORE_CLOCK / (2u * frequencyHz);
    halfPeriodLoops_ = cyclesPerHalfPeriod / 2u;
    if (halfPeriodLoops_ == 0) halfPeriodLoops_ = 1;
}

__attribute__((always_inline)) inline void Ch32Jtag::halfPeriodDelay() const
{
    uint32_t loops = halfPeriodLoops_;
    __asm volatile(
        "1: addi %0, %0, -1\n"
        "   bnez %0, 1b\n"
        : "+r"(loops));
}

void Ch32Jtag::setTck(bool high)
{
    Ch32Gpio::write(JTAG_TCK_PORT, JTAG_TCK_PIN, high);
    pinState_ = static_cast<uint8_t>((pinState_ & ~0x01u) | (high ? 0x01u : 0));
}

void Ch32Jtag::setTms(bool high)
{
    Ch32Gpio::write(JTAG_TMS_PORT, JTAG_TMS_PIN, high);
    pinState_ = static_cast<uint8_t>((pinState_ & ~0x02u) | (high ? 0x02u : 0));
}

void Ch32Jtag::setTdi(bool high)
{
    Ch32Gpio::write(JTAG_TDI_PORT, JTAG_TDI_PIN, high);
    pinState_ = static_cast<uint8_t>((pinState_ & ~0x04u) | (high ? 0x04u : 0));
}

__attribute__((always_inline)) inline void Ch32Jtag::setTmsIfChanged(bool high)
{
    if (((pinState_ & 0x02u) != 0) != high) setTms(high);
}

__attribute__((always_inline)) inline void Ch32Jtag::setTdiIfChanged(bool high)
{
    if (((pinState_ & 0x04u) != 0) != high) setTdi(high);
}

bool Ch32Jtag::getTdo() const
{
    return (JTAG_TDO_PORT->INDR & JTAG_TDO_PIN) != 0;
}

void Ch32Jtag::setTrst(bool high)
{
    pinState_ = static_cast<uint8_t>((pinState_ & ~0x20u) | (high ? 0x20u : 0));
#ifdef JTAG_TRST
    Ch32Gpio::setOpenDrain(JTAG_TRST_PORT, JTAG_TRST_PIN, high);
#else
    // nTRST is not routed. Assertion enters Test-Logic-Reset through TMS.
    // Deassertion must not add a clock; writePins() applies the requested final
    // signal levels after this reset sequence.
    if (!high)
        (void)clock(6, true, false);
#endif
}
void Ch32Jtag::setSrst(bool high)
{
    Ch32Gpio::setOpenDrain(JTAG_SRST_PORT, JTAG_SRST_PIN, high);
    pinState_ = static_cast<uint8_t>((pinState_ & ~0x80u) | (high ? 0x80u : 0));
}

__attribute__((always_inline)) inline bool Ch32Jtag::pulseClock()
{
    halfPeriodDelay();
    Ch32Gpio::write(JTAG_TCK_PORT, JTAG_TCK_PIN, true);
    halfPeriodDelay();
    const bool tdo = getTdo();
    Ch32Gpio::write(JTAG_TCK_PORT, JTAG_TCK_PIN, false);
    return tdo;
}

__attribute__((always_inline)) inline void Ch32Jtag::pulseClockNoCapture()
{
    halfPeriodDelay();
    Ch32Gpio::write(JTAG_TCK_PORT, JTAG_TCK_PIN, true);
    halfPeriodDelay();
    Ch32Gpio::write(JTAG_TCK_PORT, JTAG_TCK_PIN, false);
}

void Ch32Jtag::sequence(uint16_t bitCount, bool tms,
                        const uint8_t* input, uint8_t* output)
{
    setTmsIfChanged(tms);
    if (pinState_ & 0x01u) setTck(false);
    if (bitCount == 0) return;

    // Keep buffer checks and byte packing outside the per-bit clock loops.
    if (!input)
    {
        setTdiIfChanged(false);

        if (!output)
        {
            // Clock only: TDI stays low and TDO is intentionally ignored.
            while (bitCount--) pulseClockNoCapture();
            return;
        }

        // Capture only: TDI stays low for the complete sequence.
        while (bitCount != 0)
        {
            const uint8_t bits = bitCount >= 8u ? 8u : bitCount;
            uint8_t received = 0;

            for (uint8_t bit = 0; bit < bits; ++bit)
            {
                if (pulseClock()) received |= static_cast<uint8_t>(1u << bit);
            }

            *output++ = received;
            bitCount = static_cast<uint16_t>(bitCount - bits);
        }
        return;
    }

    if (!output)
    {
        // Shift out without paying for a TDO sample or receive packing.
        bool tdi = (pinState_ & 0x04u) != 0;
        while (bitCount != 0)
        {
            const uint8_t bits = bitCount >= 8u ? 8u : bitCount;
            uint8_t sent = *input++;

            for (uint8_t bit = 0; bit < bits; ++bit)
            {
                const bool nextTdi = (sent & 0x01u) != 0;
                if (nextTdi != tdi)
                {
                    Ch32Gpio::write(JTAG_TDI_PORT, JTAG_TDI_PIN, nextTdi);
                    tdi = nextTdi;
                }
                sent >>= 1u;
                pulseClockNoCapture();
            }

            bitCount = static_cast<uint16_t>(bitCount - bits);
        }
        pinState_ = static_cast<uint8_t>((pinState_ & ~0x04u) |
                                        (tdi ? 0x04u : 0));
        return;
    }

    // Full-duplex shift: keep GPIO and byte state in registers.
    bool tdi = (pinState_ & 0x04u) != 0;
    while (bitCount != 0)
    {
        const uint8_t bits = bitCount >= 8u ? 8u : bitCount;
        uint8_t sent = *input++;
        uint8_t received = 0;

        for (uint8_t bit = 0; bit < bits; ++bit)
        {
            const bool nextTdi = (sent & 0x01u) != 0;
            if (nextTdi != tdi)
            {
                Ch32Gpio::write(JTAG_TDI_PORT, JTAG_TDI_PIN, nextTdi);
                tdi = nextTdi;
            }
            sent >>= 1u;
            if (pulseClock()) received |= static_cast<uint8_t>(1u << bit);
        }

        *output++ = received;
        bitCount = static_cast<uint16_t>(bitCount - bits);
    }
    pinState_ = static_cast<uint8_t>((pinState_ & ~0x04u) |
                                    (tdi ? 0x04u : 0));
}

bool Ch32Jtag::clock(uint8_t pulses, bool tms, bool tdi)
{
    setTmsIfChanged(tms);
    setTdiIfChanged(tdi);
    if (pinState_ & 0x01u) setTck(false);

    if (pulses == 0) return getTdo();
    while (--pulses != 0) pulseClockNoCapture();
    return pulseClock();
}

bool Ch32Jtag::writePins(uint8_t value, uint8_t select)
{
    // Emulated nTRST clocks TMS high. Apply it before the requested signal
    // levels, with TCK last, so a combined DAP_SWJ_Pins command ends exactly
    // in the state requested by the host.
    if (select & 0x20u) setTrst((value & 0x20u) != 0);
    if (select & 0x80u) setSrst((value & 0x80u) != 0);
    if (select & 0x02u) setTms((value & 0x02u) != 0);
    if (select & 0x04u) setTdi((value & 0x04u) != 0);
    if (select & 0x01u) setTck((value & 0x01u) != 0);
    return true;
}

uint8_t Ch32Jtag::readPins() const
{
    uint8_t pins = 0;
    if (JTAG_TCK_PORT->INDR & JTAG_TCK_PIN) pins |= 0x01u;
    if (JTAG_TMS_PORT->INDR & JTAG_TMS_PIN) pins |= 0x02u;
    if (JTAG_TDI_PORT->INDR & JTAG_TDI_PIN) pins |= 0x04u;
    if (getTdo()) pins |= 0x08u;
#ifdef JTAG_TRST
    if (JTAG_TRST_PORT->INDR & JTAG_TRST_PIN) pins |= 0x20u;
#else
    // There is no physical nTRST to sample in the emulated configuration.
    pins |= static_cast<uint8_t>(pinState_ & 0x20u);
#endif
    if (JTAG_SRST_PORT->INDR & JTAG_SRST_PIN) pins |= 0x80u;
    return pins;
}

bool Ch32Jtag::resetTarget()
{
    setSrst(false);
    Delay_Ms(10);
    setSrst(true);
    return true;
}

void Ch32Jtag::delayUs(uint32_t microseconds)
{
    Delay_Us(microseconds);
}
