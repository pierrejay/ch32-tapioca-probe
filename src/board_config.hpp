#pragma once

// Default debug pinout for the CH32X035F8U6/QFN20 reference design.
// Override these definitions through EXTRA_CPPFLAGS for a custom PCB.

// TMS/TCK share the PIOC/SDI pins used by SWDIO/SWCLK. A target wired for
// two-wire SWD can therefore use JTAG without moving those two signals.
#ifndef JTAG_TMS_PORT
#define JTAG_TMS_PORT GPIOC
#define JTAG_TMS_PIN  GPIO_Pin_19
#endif

#ifndef JTAG_TCK_PORT
#define JTAG_TCK_PORT GPIOC
#define JTAG_TCK_PIN  GPIO_Pin_18
#endif

#ifndef JTAG_TDO_PORT
#define JTAG_TDO_PORT GPIOA
#define JTAG_TDO_PIN  GPIO_Pin_6
#endif

#ifndef JTAG_TDI_PORT
#define JTAG_TDI_PORT GPIOA
#define JTAG_TDI_PIN  GPIO_Pin_7
#endif

// nSRST is shared by the JTAG and CMSIS-DAP/SWD backends.
#ifndef TARGET_RESET_PORT
#define TARGET_RESET_PORT GPIOA
#define TARGET_RESET_PIN  GPIO_Pin_4
#endif

#ifndef JTAG_SRST_PORT
#define JTAG_SRST_PORT TARGET_RESET_PORT
#define JTAG_SRST_PIN  TARGET_RESET_PIN
#endif

// Optional physical JTAG test reset. Without JTAG_TRST, the backend emulates
// nTRST through the standard TMS Test-Logic-Reset sequence instead.
#ifndef JTAG_TRST_PORT
#define JTAG_TRST_PORT GPIOA
#define JTAG_TRST_PIN  GPIO_Pin_5
#endif

#ifndef JTAG_DEFAULT_FREQUENCY_KHZ
#define JTAG_DEFAULT_FREQUENCY_KHZ 500
#endif

#ifndef LOAD_SWITCH_PIN
#define LOAD_SWITCH_PIN PA3
#endif

#ifndef LED_PIN
#define LED_PIN PA2
#endif
