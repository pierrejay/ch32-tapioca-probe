#pragma once

#include "ch32_sdk.hpp"

// ch32fun's encoded-pin helpers stop at CFGHR and expect one encoded pin.
// These primitives also cover the CH32X035's pins 16..23 in CFGXR and accept
// the port-pointer/runtime-mask form used by the transport backends.
namespace Ch32Gpio
{
inline void configure(GPIO_TypeDef* port, uint32_t pins,
                      GPIO_CFGLR_PIN_MODE_Typedef mode)
{
    for (uint32_t bit = 0; bit < 24; ++bit)
    {
        if ((pins & (1u << bit)) == 0) continue;
        volatile uint32_t* cfg = bit < 8 ? &port->CFGLR :
                                 bit < 16 ? &port->CFGHR : &port->CFGXR;
        const uint32_t shift = (bit & 7u) * 4u;
        *cfg = (*cfg & ~(0xfu << shift)) |
               (static_cast<uint32_t>(mode) << shift);
    }
}

inline void floatPins(GPIO_TypeDef* port, uint32_t pins)
{
    configure(port, pins, GPIO_CFGLR_IN_FLOAT);
}

inline void write(GPIO_TypeDef* port, uint32_t pins, bool high)
{
    if (high)
    {
        const uint32_t lowPins = pins & 0xffffu;
        const uint32_t highPins = pins >> 16u;
        if (lowPins) port->BSHR = lowPins;
        if (highPins) port->BSXR = highPins;
    }
    else
    {
        port->BCR = pins;
    }
}

inline void setOpenDrain(GPIO_TypeDef* port, uint32_t pins, bool high)
{
    // A high level releases the line completely; a low level is actively
    // driven without ever sourcing the target rail.
    if (high)
    {
        floatPins(port, pins);
    }
    else
    {
        port->BCR = pins;
        configure(port, pins, GPIO_CFGLR_OUT_10Mhz_PP);
        port->BCR = pins;
    }
}
}
