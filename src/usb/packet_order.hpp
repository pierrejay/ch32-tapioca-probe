#pragma once

#include <stdint.h>

// Sequence counters are compared modulo 2^32. This remains unambiguous while
// fewer than 2^31 packets are simultaneously pending (the hardware holds two).
constexpr bool packetArrivedBefore(uint32_t left, uint32_t right)
{
    return static_cast<int32_t>(left - right) < 0;
}
