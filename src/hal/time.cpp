// TIM2-backed monotonic timebase.
#include "time.hpp"
#include "ch32_sdk.hpp"

namespace
{
volatile uint32_t g_millis = 0;
bool g_timeInitialized = false;
#ifdef UART_BRIDGE
Time::TickHandler volatile g_tickHandler = nullptr;
#endif
}

extern "C" void TIM2_IRQHandler(void) INTERRUPT_DECORATOR;
extern "C" void TIM2_IRQHandler(void)
{
    if ((TIM2->INTFR & TIM_UIF) != 0)
    {
        ++g_millis;
        TIM2->INTFR = static_cast<uint16_t>(~TIM_UIF);
#ifdef UART_BRIDGE
        const Time::TickHandler handler = g_tickHandler;
        if (handler) handler();
#endif
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

#ifdef UART_BRIDGE
void setTickHandler(TickHandler handler)
{
    g_tickHandler = handler;
}
#endif

uint32_t millis()
{
    return g_millis;
}

uint32_t micros()
{
    for (;;)
    {
        const uint32_t before = g_millis;
        uint32_t counter = TIM2->CNT;
        const bool updatePending = (TIM2->INTFR & TIM_UIF) != 0;
        const uint32_t after = g_millis;

        // The ISR ran while the split counter was sampled. Retry with two
        // values belonging to the same millisecond.
        if (before != after) continue;

        if (updatePending)
        {
            // TIM2 already wrapped, but its ISR may not have incremented
            // g_millis yet. Re-read CNT on the new side of the rollover. If
            // the ISR catches up meanwhile, retry instead of counting twice.
            counter = TIM2->CNT;
            if (g_millis != before) continue;
            return (before + 1u) * 1000u + counter;
        }

        return before * 1000u + counter;
    }
}

} // namespace Time
