// TIM2-backed monotonic timebase.
#include "time.hpp"
#include "ch32_sdk.hpp"

namespace
{
volatile uint32_t g_millis = 0;
bool g_timeInitialized = false;
}

extern "C" void TIM2_IRQHandler(void) INTERRUPT_DECORATOR;
extern "C" void TIM2_IRQHandler(void)
{
    if ((TIM2->INTFR & TIM_UIF) != 0)
    {
        ++g_millis;
        TIM2->INTFR = static_cast<uint16_t>(~TIM_UIF);
    }
}

namespace Time {

void init()
{
    if (g_timeInitialized)
    {
        return;
    }

    RCC->APB1PCENR |= RCC_APB1Periph_TIM2;
    TIM2->CTLR1 = 0;
    TIM2->PSC = static_cast<uint16_t>(FUNCONF_SYSTEM_CORE_CLOCK / 1000000u - 1u);
    TIM2->ATRLR = 999;
    TIM2->CNT = 0;
    TIM2->SWEVGR = TIM_UG;
    TIM2->INTFR = static_cast<uint16_t>(~TIM_UIF);
    TIM2->DMAINTENR = TIM_UIE;
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CTLR1 = TIM_CEN;

    g_timeInitialized = true;
}

uint32_t millis()
{
    return g_millis;
}

uint32_t micros()
{
    // TIM2 counts 0..999 at 1 MHz and rolls the ms counter on overflow. Read the
    // ms counter on both sides of the CNT read; if it ticked over, re-read CNT
    // against the new ms so the two halves are consistent.
    uint32_t m1 = g_millis;
    uint32_t cnt = TIM2->CNT;
    uint32_t m2 = g_millis;
    if (m1 != m2)
    {
        cnt = TIM2->CNT;
        m1  = m2;
    }
    return m1 * 1000u + cnt;
}

} // namespace Time
