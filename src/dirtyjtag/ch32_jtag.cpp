#include "ch32_jtag.hpp"
#include "board_config.hpp"

void Ch32Jtag::configureOutput(GPIO_TypeDef* port, uint32_t pin)
{
    GPIO_InitTypeDef gpio = {};
    gpio.GPIO_Pin = pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(port, &gpio);
}

void Ch32Jtag::configureInput(GPIO_TypeDef* port, uint32_t pin, GPIOMode_TypeDef mode)
{
    GPIO_InitTypeDef gpio = {};
    gpio.GPIO_Pin = pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = mode;
    GPIO_Init(port, &gpio);
}

void Ch32Jtag::write(GPIO_TypeDef* port, uint32_t pin, bool high)
{
    if (high) GPIO_SetBits(port, pin);
    else      GPIO_ResetBits(port, pin);
}

void Ch32Jtag::setResetLine(GPIO_TypeDef* port, uint32_t pin, bool high)
{
    // The CH32X035 GPIO API does not expose an open-drain output mode. Emulate
    // one safely: output-low asserts reset, floating input releases it so the
    // target-side pull-up defines the high level.
    if (high)
    {
        configureInput(port, pin, GPIO_Mode_IN_FLOATING);
    }
    else
    {
        GPIO_ResetBits(port, pin);
        configureOutput(port, pin);
        GPIO_ResetBits(port, pin);
    }
}

void Ch32Jtag::init()
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    // TCK/TMS are on PC18/PC19, the chip's SDI debug pins - free them for GPIO
    // use (idempotent; the SWD backend does the same for PIOC).
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    setFrequencyKhz(DJTAG_DEFAULT_FREQUENCY_KHZ);
    disconnect();
}

void Ch32Jtag::activate()
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    // TCK/TMS are on PC18/PC19, the chip's SDI debug pins - free them for GPIO
    // use (idempotent; the SWD backend does the same for PIOC).
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    configureOutput(DJTAG_TCK_PORT, DJTAG_TCK_PIN);
    configureOutput(DJTAG_TDI_PORT, DJTAG_TDI_PIN);
    configureOutput(DJTAG_TMS_PORT, DJTAG_TMS_PIN);
    configureInput(DJTAG_TDO_PORT, DJTAG_TDO_PIN, GPIO_Mode_IPD);

    setTck(false);
    setTdi(false);
    setTms(false);
    setSrst(true);
    setTrst(true);
}

void Ch32Jtag::disconnect()
{
    configureInput(DJTAG_TCK_PORT, DJTAG_TCK_PIN, GPIO_Mode_IN_FLOATING);
    configureInput(DJTAG_TDI_PORT, DJTAG_TDI_PIN, GPIO_Mode_IN_FLOATING);
    configureInput(DJTAG_TMS_PORT, DJTAG_TMS_PIN, GPIO_Mode_IN_FLOATING);
    configureInput(DJTAG_TDO_PORT, DJTAG_TDO_PIN, GPIO_Mode_IN_FLOATING);
    setSrst(true);
    setTrst(true);
}

void Ch32Jtag::setFrequencyKhz(uint16_t frequencyKhz)
{
    if (frequencyKhz == 0) frequencyKhz = 1;
    if (frequencyKhz > 2000) frequencyKhz = 2000;

    // The loop and GPIO accesses dominate. This estimate intentionally errs on
    // the slow side; hardware validation can later replace it with a timer/SPI
    // implementation without changing the protocol core.
    const uint32_t cyclesPerHalfPeriod = SystemCoreClock / (2u * frequencyKhz * 1000u);
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
    return GPIO_ReadInputDataBit(DJTAG_TDO_PORT, DJTAG_TDO_PIN) != Bit_RESET;
}

void Ch32Jtag::setTrst(bool high) { setResetLine(DJTAG_TRST_PORT, DJTAG_TRST_PIN, high); }
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
