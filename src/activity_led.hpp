// Activity LED, defaulting to the reference board's active-high PA2.
// Override LED_PIN with a ch32fun pin name; LED_PIN=-1 disables it.
#pragma once

#include "ch32_sdk.hpp"
#include "time.hpp"

#ifndef LED_PIN
#define LED_PIN PA2
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
    funGpioInitAll();
    funPinMode(LED_PIN, GPIO_CFGLR_OUT_10Mhz_PP);
    funDigitalWrite(LED_PIN, FUN_LOW);
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
    funDigitalWrite(LED_PIN, on ? FUN_HIGH : FUN_LOW);
}

#endif
} // namespace ActivityLed
