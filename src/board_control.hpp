// Target power and activity LED for the reference board.
#pragma once

#include "board_config.hpp"
#include "ch32_sdk.hpp"
#include "time.hpp"

namespace LoadSwitch
{
#if LOAD_SWITCH_PIN < 0

inline void init() {}
inline void enable(bool) {}
inline bool enabled() { return true; }

#else

inline void enable(bool enabled)
{
    if (enabled)
    {
        // Release EN before selecting its weak pull-up. The external pull-up
        // keeps target power on through reset, bootloader entry and crashes.
        funPinMode(LOAD_SWITCH_PIN, GPIO_CFGLR_IN_FLOAT);
        funDigitalWrite(LOAD_SWITCH_PIN, FUN_HIGH);
        funPinMode(LOAD_SWITCH_PIN, GPIO_CFGLR_IN_PUPD);
    }
    else
    {
        // Open-drain low is the only actively driven state.
        funDigitalWrite(LOAD_SWITCH_PIN, FUN_LOW);
        funPinMode(LOAD_SWITCH_PIN, GPIO_CFGLR_OUT_2Mhz_OD);
        funDigitalWrite(LOAD_SWITCH_PIN, FUN_LOW);
    }
}

inline bool enabled()
{
    return funDigitalRead(LOAD_SWITCH_PIN) != 0;
}

inline void init()
{
    funGpioInitAll();
    enable(true);
}

#endif
} // namespace LoadSwitch

namespace ActivityLed
{
#if LED_PIN < 0

inline void init() {}
inline void notify() {}
inline void tick() {}

#else

inline uint32_t& lastActivityMs()
{
    // Starts at 0: LED flickers briefly at power-on.
    static uint32_t t = 0;
    return t;
}

inline void init()
{
    funGpioInitAll();
    funPinMode(LED_PIN, GPIO_CFGLR_OUT_10Mhz_PP);
    funDigitalWrite(LED_PIN, LoadSwitch::enabled() ? FUN_HIGH : FUN_LOW);
}

inline void notify()
{
    lastActivityMs() = Time::millis();
}

inline void tick()
{
    const uint32_t now = Time::millis();
    const bool active = (int32_t)(now - lastActivityMs()) < 200;
    const bool flicker = active && ((now / 60) & 1u);
    const bool on = LoadSwitch::enabled() != flicker;
    funDigitalWrite(LED_PIN, on ? FUN_HIGH : FUN_LOW);
}

#endif
} // namespace ActivityLed

namespace BoardControl
{
inline void init()
{
    LoadSwitch::init();
    ActivityLed::init();
}
} // namespace BoardControl
