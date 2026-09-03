// WCH-LinkE-compatible USB probe. The default build detects RVSWIO or RVSWD;
// WCH_TRANSPORT_RVSWD and WCH_TRANSPORT_RVSWIO select one for diagnostics.

#include "ch32_sdk.hpp"
#include "time.hpp"
#include "board_control.hpp"
#include "protocol.hpp"
#ifdef UART_BRIDGE
#include "scoped_irq_mask.hpp"
#include "uart_bridge.hpp"
#endif
#include "usb_wchlink.hpp"

#if defined(WCH_TRANSPORT_RVSWD)
#include "ch32_pioc_rvswd.hpp"
using WchPort = Ch32PiocRvswd;
#elif defined(WCH_TRANSPORT_RVSWIO)
#include "ch32_pioc_rvswio.hpp"
using WchPort = Ch32PiocRvswio;
#else
#include "ch32_wch_autoport.hpp"
using WchPort = Ch32WchAutoPort;
#endif

#ifdef WCH_EMIT_SELFTEST

// Emit a repeated DMCONTROL write for logic-analyser measurements.
// Frame: DMI 0x10 = 0x80000001.
static WchPort g_emitPort;

int main(void)
{
    SystemInit();
    Time::init(); // deferred RVSWD guard reads micros(); must run or runFrame spins

    g_emitPort.init();
    while (1)
    {
        g_emitPort.writeDmi(0x10, 0x80000001);
        Delay_Ms(5); // clear idle gap between frames for easy triggering
    }
}

#else

static UsbWchLink g_usb;
static WchPort g_port;
#ifdef UART_BRIDGE
static UartBridge g_uart;
static void requestUartRxDrain() { g_usb.requestUartRxDrain(); }
#endif

static void switchDutOn(bool enabled)
{
    // Park the target-facing UART pins before power-off to avoid back-powering
    // the target, and restore them only after its rail is back up. The USB IRQ
    // also drains UART RX, so exclude it while the DMA data path is rebuilt.
#ifdef UART_BRIDGE
    ScopedIrqMask usbIrq(USBFS_IRQn);
    if (!enabled) g_uart.suspend();
#endif
    LoadSwitch::enable(enabled);
#ifdef UART_BRIDGE
    if (enabled)
    {
        // The load switch rises in about 0.6 ms. Keep UART pins isolated until
        // the target rail is established instead of feeding it through PB0.
        Delay_Us(1000);
        g_uart.resume();
    }
#endif
}

static WchLink::Core g_core;

int main(void)
{
    SystemInit();
    Time::init(); // microsecond clock for the deferred RVSWD inter-frame guard
    BoardControl::init();
    g_core.init(switchDutOn);
#ifdef UART_BRIDGE
    g_uart.init();
    g_usb.init(&g_uart);
    Time::setTickHandler(requestUartRxDrain);
#else
    g_usb.init(nullptr);
#endif
    g_port.init();

    uint8_t rx[UsbWchLink::kPacketSize];
    uint8_t tx[UsbWchLink::kPacketSize];

    while (1)
    {
        ActivityLed::tick(); // before the idle continue, so the LED updates every loop
#ifdef UART_BRIDGE
        if (g_usb.pollCdc()) ActivityLed::notify();
#endif

        if (g_usb.takeSessionReset()) g_core.reset(g_port);

        size_t rxLength = 0;
        if (!g_usb.takeNextPacket(rx, rxLength)) continue;

        const WchLink::Result result =
            g_core.processPacket(g_port, rx, rxLength, tx, sizeof(tx));
        ActivityLed::notify(); // a host command was handled (mostly target DMI while flashing)

        // The decoder guarantees a non-empty reply for every command, so the host
        // never stalls waiting on the mandatory EP1 IN read.
        g_usb.finish(tx, result.responseLength);
    }
}

#endif // WCH_EMIT_SELFTEST
