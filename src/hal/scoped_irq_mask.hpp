#pragma once

#include "ch32_sdk.hpp"

// Temporarily masks one IRQ and restores its previous enable state on exit.
class ScopedIrqMask
{
public:
    explicit ScopedIrqMask(IRQn_Type irq)
        : irq_(irq), wasEnabled_(NVIC_GetStatusIRQ(irq) != 0)
    {
        NVIC_DisableIRQ(irq_);
    }

    ~ScopedIrqMask()
    {
        if (wasEnabled_) NVIC_EnableIRQ(irq_);
    }

    ScopedIrqMask(const ScopedIrqMask&) = delete;
    ScopedIrqMask& operator=(const ScopedIrqMask&) = delete;

private:
    IRQn_Type irq_;
    bool wasEnabled_;
};
