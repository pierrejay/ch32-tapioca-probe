#include "ch32_jtag.hpp"
#include "board_config.hpp"

void Ch32Jtag::configureOutput(GPIO_TypeDef* port, uint32_t pin)
{
    configureInput(port, pin, GPIO_CFGLR_OUT_10Mhz_PP);
}

void Ch32Jtag::configureInput(GPIO_TypeDef* port, uint32_t pins,
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

void Ch32Jtag::write(GPIO_TypeDef* port, uint32_t pin, bool high)
{
    if (high)
    {
        port->BSHR = pin & 0xffffu;
        port->BSXR = pin >> 16u;
    }
    else
    {
        port->BCR = pin;
    }
}

void Ch32Jtag::setResetLine(GPIO_TypeDef* port, uint32_t pin, bool high)
{
    // The CH32X035 GPIO API does not expose an open-drain output mode. Emulate
    // one safely: output-low asserts reset, floating input releases it so the
    // target-side pull-up defines the high level.
    if (high)
    {
        configureInput(port, pin, GPIO_CFGLR_IN_FLOAT);
    }
    else
    {
        port->BCR = pin;
        configureOutput(port, pin);
        port->BCR = pin;
    }
}

void Ch32Jtag::init()
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                      RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
    // TCK/TMS are on PC18/PC19, the chip's SDI debug pins - free them for GPIO
    // use (idempotent; the SWD backend does the same for PIOC).
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_SWJ_CFG) | AFIO_PCFR1_SWJ_CFG_DISABLE;

    setFrequencyKhz(DJTAG_DEFAULT_FREQUENCY_KHZ);
    disconnect();
}

void Ch32Jtag::activate()
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                      RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
    // TCK/TMS are on PC18/PC19, the chip's SDI debug pins - free them for GPIO
    // use (idempotent; the SWD backend does the same for PIOC).
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_SWJ_CFG) | AFIO_PCFR1_SWJ_CFG_DISABLE;

    configureOutput(DJTAG_TCK_PORT, DJTAG_TCK_PIN);
    configureOutput(DJTAG_TDI_PORT, DJTAG_TDI_PIN);
    configureOutput(DJTAG_TMS_PORT, DJTAG_TMS_PIN);
    DJTAG_TDO_PORT->BCR = DJTAG_TDO_PIN;
    configureInput(DJTAG_TDO_PORT, DJTAG_TDO_PIN, GPIO_CFGLR_IN_PUPD);

    setTck(false);
    setTdi(false);
    setTms(false);
    setSrst(true);
    setTrst(true);
    selectJtag();
}

void Ch32Jtag::disconnect()
{
    configureInput(DJTAG_TCK_PORT, DJTAG_TCK_PIN, GPIO_CFGLR_IN_FLOAT);
    configureInput(DJTAG_TDI_PORT, DJTAG_TDI_PIN, GPIO_CFGLR_IN_FLOAT);
    configureInput(DJTAG_TMS_PORT, DJTAG_TMS_PIN, GPIO_CFGLR_IN_FLOAT);
    configureInput(DJTAG_TDO_PORT, DJTAG_TDO_PIN, GPIO_CFGLR_IN_FLOAT);
    setSrst(true);
#ifdef JTAG_TRST
    setTrst(true);
#endif
}

void Ch32Jtag::setFrequencyKhz(uint16_t frequencyKhz)
{
    if (frequencyKhz == 0) frequencyKhz = 1;
    if (frequencyKhz > 2000) frequencyKhz = 2000;

    // The loop and GPIO accesses dominate. This estimate intentionally errs on
    // the slow side; hardware validation can later replace it with a timer/SPI
    // implementation without changing the protocol core.
    const uint32_t cyclesPerHalfPeriod = FUNCONF_SYSTEM_CORE_CLOCK / (2u * frequencyKhz * 1000u);
    halfPeriodLoops_ = cyclesPerHalfPeriod / 4u;
    if (halfPeriodLoops_ == 0) halfPeriodLoops_ = 1;
}

void Ch32Jtag::halfPeriodDelay() const
{
    for (volatile uint32_t i = 0; i < halfPeriodLoops_; ++i)
        __asm volatile("nop");
}

void Ch32Jtag::setTck(bool high) { write(DJTAG_TCK_PORT, DJTAG_TCK_PIN, high); }
void Ch32Jtag::setTdi(bool high) { write(DJTAG_TDI_PORT, DJTAG_TDI_PIN, high); }
void Ch32Jtag::setTms(bool high) { write(DJTAG_TMS_PORT, DJTAG_TMS_PIN, high); }

bool Ch32Jtag::getTdo() const
{
    return (DJTAG_TDO_PORT->INDR & DJTAG_TDO_PIN) != 0;
}

void Ch32Jtag::setTrst(bool high)
{
#ifdef JTAG_TRST
    setResetLine(DJTAG_TRST_PORT, DJTAG_TRST_PIN, high);
#else
    // nTRST is not routed. Assertion enters Test-Logic-Reset through TMS.
    // Deassertion must not add a clock: SETSIG has already applied the TMS
    // level requested by the host for the state that should follow reset.
    if (!high)
        (void)clock(6, true, false);
#endif
}
void Ch32Jtag::setSrst(bool high) { setResetLine(DJTAG_SRST_PORT, DJTAG_SRST_PIN, high); }

bool Ch32Jtag::pulseClock()
{
    halfPeriodDelay();
    setTck(true);
    halfPeriodDelay();
    const bool tdo = getTdo();
    setTck(false);
    return tdo;
}

void Ch32Jtag::selectJtag()
{
    // Arm SWJ-DP switch sequence: line reset, 0xE73C LSB-first, JTAG reset.
    (void)clock(51, true, false);
    constexpr uint16_t sequence = 0xe73c;
    for (uint8_t bit = 0; bit < 16; ++bit)
    {
        setTms((sequence & (1u << bit)) != 0);
        (void)pulseClock();
    }
    (void)clock(6, true, false);
    setTms(false);
}

void Ch32Jtag::transfer(uint16_t bitCount, const uint8_t* in, uint8_t* out)
{
    setTms(false);

    for (uint16_t bit = 0; bit < bitCount; ++bit)
    {
        const uint8_t mask = (uint8_t)(0x80u >> (bit & 7u));
        setTdi((in[bit >> 3u] & mask) != 0);
        const bool tdo = pulseClock();
        if (out && tdo) out[bit >> 3u] |= mask;
    }
}

bool Ch32Jtag::clock(uint8_t pulses, bool tms, bool tdi)
{
    setTms(tms);
    setTdi(tdi);

    bool lastTdo = getTdo();
    while (pulses--) lastTdo = pulseClock();
    return lastTdo;
}
