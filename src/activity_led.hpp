// activity_led.hpp - activity LED: flickers while the probe is talking to a target,
// off when idle. Default = WeAct CH32X035 on-board LED (PB12, active-high).
//
// notify() on each target transaction (stamps the time), tick() every main-loop
// iteration (drives the pin). No timer, no delay.
//
// Override the pin with -D LED_PORT=GPIOA and/or -D LED_PIN=<0..15>.
// Disable it entirely with -D LED_PIN=-1 (the pin is left untouched).
#pragma once

#include "ch32_sdk.hpp"
#include "time.hpp"

#ifndef LED_PORT
#define LED_PORT GPIOB
#endif
#ifndef LED_PIN
#define LED_PIN 12
#endif

namespace ActivityLed
{
#if LED_PIN < 0 // disabled

inline void init() {}
inline void notify() {}
inline void tick() {}

#else

inline uint32_t& lastActivityMs()
{
    // Starts at 0: LED blinks briefly at power-on
    static uint32_t t = 0;
    return t;
}

inline void init()
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                               RCC_APB2Periph_GPIOC,
                           ENABLE);
    GPIO_InitTypeDef gpio = {};
    gpio.GPIO_Pin = 1u << LED_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_ResetBits(LED_PORT, 1u << LED_PIN);
}

inline void notify()
{
    lastActivityMs() = Time::millis();
}

inline void tick()
{
    const uint32_t now = Time::millis();
    const bool active = (int32_t)(now - lastActivityMs()) < 200; // recent traffic
    const bool on = active && ((now / 60) & 1u);                 // ~8 Hz flicker
    if (on)
        GPIO_SetBits(LED_PORT, 1u << LED_PIN);
    else
        GPIO_ResetBits(LED_PORT, 1u << LED_PIN);
}

#endif
} // namespace ActivityLed
