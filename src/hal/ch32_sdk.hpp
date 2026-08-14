// Single C++ include point for ch32fun.
#pragma once

extern "C" {
#include "ch32fun.h"
}

// ch32fun exposes the X035 PIOC base address but not its mailbox register map.
// Keep the small subset used by the probe here, next to the device definitions.
#define PIOC_SRAM_BASE       (SRAM_BASE + 0x4000u)
#define R8_SYS_CFG           (*reinterpret_cast<volatile uint8_t*>(PIOC_BASE + 0x1cu))
#define R8_DATA_REG(n)       (*reinterpret_cast<volatile uint8_t*>(PIOC_BASE + 0x20u + (n)))
#define R32_DATA_REG(n)      (*reinterpret_cast<volatile uint32_t*>(PIOC_BASE + 0x20u + (n)))
#define R8_DATA_REG0         R8_DATA_REG(0)
#define R8_DATA_REG1         R8_DATA_REG(1)
#define R8_DATA_REG2         R8_DATA_REG(2)
#define R8_DATA_REG3         R8_DATA_REG(3)
#define R8_DATA_REG4         R8_DATA_REG(4)
#define R8_DATA_REG5         R8_DATA_REG(5)
#define R8_DATA_REG6         R8_DATA_REG(6)
#define R8_DATA_REG7         R8_DATA_REG(7)
#define R8_DATA_REG8         R8_DATA_REG(8)
#define R8_DATA_REG9         R8_DATA_REG(9)
#define R8_DATA_REG10        R8_DATA_REG(10)
#define R8_DATA_REG11        R8_DATA_REG(11)
#define R8_DATA_REG12        R8_DATA_REG(12)
#define R8_DATA_REG13        R8_DATA_REG(13)
#define R8_DATA_REG14        R8_DATA_REG(14)
#define R8_DATA_REG15        R8_DATA_REG(15)
#define R32_DATA_REG4_7      R32_DATA_REG(4)
#define R32_DATA_REG8_11     R32_DATA_REG(8)
#define R32_DATA_REG12_15    R32_DATA_REG(12)

#define RB_MST_IO_EN1        0x08u
#define RB_MST_IO_EN0        0x04u
#define RB_MST_RESET         0x02u
#define RB_MST_CLK_GATE      0x01u

// WCH's peripheral library calls this block USBFSD; ch32fun uses USBFS.
#define USBFSD USBFS
