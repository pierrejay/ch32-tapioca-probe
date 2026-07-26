#pragma once

#include <stdint.h>

namespace PiocSwdProtocol
{
constexpr uint8_t parity4(uint8_t value)
{
    value &= 0x0f;
    value ^= value >> 2;
    value ^= value >> 1;
    return value & 1u;
}

// Wire byte, least-significant bit first:
// start, APnDP, RnW, A2, A3, request parity, stop, park.
constexpr uint8_t requestFrame(uint8_t request)
{
    return (uint8_t)(0x81u | ((request & 0x0fu) << 1u) | (parity4(request) << 5u));
}

constexpr uint8_t parity32(uint32_t value)
{
    value ^= value >> 16;
    value ^= value >> 8;
    value ^= value >> 4;
    value &= 0x0f;
    return (uint8_t)((0x6996u >> value) & 1u);
}

constexpr bool validAck(uint8_t ack)
{
    return ack == 1u || ack == 2u || ack == 4u;
}
}
