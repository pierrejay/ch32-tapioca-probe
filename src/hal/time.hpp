// time.hpp - TIM2-backed monotonic timebase (lifted from the sibling Tapioca HAL).
#pragma once

#include <stdint.h>

namespace Time {

void init();
uint32_t millis();

// Microsecond clock: ms counter * 1000 + the 1us-resolution TIM2 counter.
// Wraps at 2^32 us (~71 min); use only for short deltas.
uint32_t micros();

} // namespace Time
