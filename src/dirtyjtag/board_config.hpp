#pragma once

// Default pinout for the CH32X035F8U6/QFN20 development target.
// Override these definitions through EXTRA_CPPFLAGS for a custom PCB.

// TMS/TCK live on the PIOC/SDI pins PC19/PC18 so they coincide with the SWD
// SWDIO/SWCLK pins: a target wired for SWD (SWDIO->PC19, SWCLK->PC18) also works
// for JTAG (TMS=PC19, TCK=PC18) with no rewiring - the SWJ-DP convenience the
// pre-PIOC build had. TDI/TDO/reset stay on their own pins (JTAG-only signals).
#ifndef DJTAG_TMS_PORT
#define DJTAG_TMS_PORT GPIOC
#define DJTAG_TMS_PIN  GPIO_Pin_19
#endif

#ifndef DJTAG_TCK_PORT
#define DJTAG_TCK_PORT GPIOC
#define DJTAG_TCK_PIN  GPIO_Pin_18
#endif

#ifndef DJTAG_TDO_PORT
#define DJTAG_TDO_PORT GPIOA
#define DJTAG_TDO_PIN  GPIO_Pin_6
#endif

#ifndef DJTAG_TDI_PORT
#define DJTAG_TDI_PORT GPIOA
#define DJTAG_TDI_PIN  GPIO_Pin_7
#endif

#ifndef DJTAG_SRST_PORT
#define DJTAG_SRST_PORT GPIOB
#define DJTAG_SRST_PIN  GPIO_Pin_0
#endif

#ifndef DJTAG_TRST_PORT
#define DJTAG_TRST_PORT GPIOB
#define DJTAG_TRST_PIN  GPIO_Pin_1
#endif

#ifndef DJTAG_DEFAULT_FREQUENCY_KHZ
#define DJTAG_DEFAULT_FREQUENCY_KHZ 500
#endif
