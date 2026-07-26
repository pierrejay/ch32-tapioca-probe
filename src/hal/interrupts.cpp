#include "ch32_sdk.hpp"

extern "C" void NMI_Handler(void)       __attribute__((interrupt("WCH-Interrupt-fast")));
extern "C" void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

extern "C" void NMI_Handler(void)
{
    while (1) { }
}

extern "C" void HardFault_Handler(void)
{
    NVIC_SystemReset();
    while (1) { }
}

