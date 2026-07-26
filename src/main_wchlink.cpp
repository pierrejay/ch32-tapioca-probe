// WCH-Link Tapioca product entry point.
//
// Second build-time-selected personality. A WCH-LinkE-compatible USB probe
// (1a86:8010, iface 0, EP1 OUT/IN) whose commands are decoded by WchLink::Core and
// executed against a IDmi. It shares nothing at link time with the
// DirtyJTAG/CMSIS-DAP product beyond the HAL. See docs/wch-link-usb-protocol.md for
// the host command set and docs/wch-rvswd-protocol.md / docs/wch-rvswio-protocol.md
// for the two wire transports.
//
// Transport is selected at build time:
//   default                  -> Ch32WchAutoPort (SHIPPING product: auto-detects
//                               RVSWD vs RVSWIO per target, like a real WCH-LinkE)
//   -D WCH_TRANSPORT_RVSWD   -> Ch32PiocRvswd  (two-wire only; bring-up/debug)
//   -D WCH_TRANSPORT_RVSWIO  -> Ch32PiocRvswio (one-wire only; bring-up/debug)
// The emit self-test needs a concrete transport, so its envs set one explicitly.

#include "ch32_sdk.hpp"
#include "time.hpp"
#include "activity_led.hpp"
#include "protocol.hpp"
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

// Bring-up self-test (env:wchlink-rvswio-emit / env:wchlink-rvswd-emit).
// Continuously emits a known DMI write frame so the wire can be captured on a
// logic analyser with no host interaction and no target required.
//   Frame: write DMI 0x10 (DMCONTROL) = 0x80000001.
// On RVSWD this clocks a 48-bit host frame on SWCLK(PC18)/SWDIO(PC19); on RVSWIO
// it is a pulse-width-encoded frame on PC19. Either way it confirms bit order and
// timing before the full USB/HIL path is exercised.
static WchPort g_emitPort;

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
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
static WchLink::Core g_core;

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    Time::init(); // microsecond clock for the deferred RVSWD inter-frame guard

    g_usb.init();
    g_port.init();
    ActivityLed::init();

    uint8_t rx[UsbWchLink::kPacketSize];
    uint8_t tx[UsbWchLink::kPacketSize];

    while (1)
    {
        ActivityLed::tick(); // before the idle continue, so the LED updates every loop

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
