// time.cpp - TIM2-backed monotonic timebase (lifted from the sibling Tapioca HAL).
#include "time.hpp"
#include "ch32_sdk.hpp"

namespace
{
volatile uint32_t g_millis = 0;
bool g_timeInitialized = false;
}

extern "C" void TIM2_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
extern "C" void TIM2_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
    {
        ++g_millis;
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

namespace Time {

void init()
{
    if (g_timeInitialized)
    {
        return;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseInitTypeDef timer = {0};
    timer.TIM_Period = 1000 - 1;
    timer.TIM_Prescaler = (uint16_t)(SystemCoreClock / 1000000u - 1u);
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &timer);

    TIM_SetCounter(TIM2, 0);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel = TIM2_UP_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 1;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, ENABLE);

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
    uint32_t cnt = TIM_GetCounter(TIM2);
    uint32_t m2 = g_millis;
    if (m1 != m2)
    {
        cnt = TIM_GetCounter(TIM2);
        m1  = m2;
    }
    return m1 * 1000u + cnt;
}

} // namespace Time
