#include "ch32_sdk.hpp"
#include "ch32_jtag.hpp"
#include "ch32_pioc_swd.hpp"
#include "cmsis_dap.hpp"
#include "protocol.hpp"
#include "time.hpp"
#include "activity_led.hpp"
#include "usb_dirtyjtag.hpp"

// JTAG and SWD may retain ownership until they disconnect or become idle.
enum class WireMode : uint8_t { None, Jtag, Swd };

constexpr bool canUseWire(WireMode owner, WireMode requester)
{
    return requester != WireMode::None &&
           (owner == WireMode::None || owner == requester);
}

constexpr WireMode releaseWire(WireMode owner, WireMode requester)
{
    return owner == requester ? WireMode::None : owner;
}

static Ch32Jtag g_jtag;
static Ch32PiocSwd g_swd;
static CmsisDap::Core g_dap;
static UsbDirtyJtag g_usb;

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    Time::init();

    g_swd.init();
    g_jtag.init();
    g_usb.init();
    ActivityLed::init();

    // A wire owner silent this long is treated as stale (its client exited
    // without releasing) and may be preempted when the other transport claims.
    // A live debugger polls its target far more often than this, so an active
    // session is never stolen. This avoids manual release / replug when
    // switching JTAG <-> SWD.
    constexpr uint32_t kIdleReleaseMs = 1000;

    WireMode wireMode = WireMode::None;
    uint32_t lastActivityMs = Time::millis();

    uint8_t rx[DirtyJtag::kPacketSize];
    uint8_t tx[DirtyJtag::kPacketSize];

    while (1)
    {
        ActivityLed::tick(); // before the idle continue, so the LED updates every loop

        if (g_usb.takeSessionReset())
        {
            g_jtag.disconnect();
            g_dap.resetConnection(g_swd);
            wireMode = WireMode::None;
        }

        size_t rxLength = 0;
        bool cmsisDap = false;
        if (!g_usb.takeNextPacket(rx, rxLength, cmsisDap)) continue;
        ActivityLed::notify(); // a JTAG or DAP packet arrived (target activity)

        if (cmsisDap)
        {
            // Preempt a stale JTAG owner: a DAP_Connect while JTAG holds the wire
            // but has been idle past the threshold releases JTAG so this SWD
            // connect can take it (JTAG clients exit without a protocol release).
            if (wireMode == WireMode::Jtag && rxLength >= 2 &&
                rx[0] == CmsisDap::kCommandConnect &&
                (uint32_t)(Time::millis() - lastActivityMs) > kIdleReleaseMs)
            {
                g_jtag.disconnect();
                wireMode = WireMode::None;
            }

            CmsisDap::Result result = {CmsisDap::Status::InvalidPacket, 0};

            // DAP_Info remains available for probe discovery. A DAP_Connect while
            // JTAG is still ACTIVE receives the standard "port disabled" response;
            // pre-connect wire commands are rejected by the CMSIS-DAP core.
            if (wireMode == WireMode::Jtag && rxLength >= 2 &&
                rx[0] == CmsisDap::kCommandConnect)
            {
                tx[0] = CmsisDap::kCommandConnect;
                tx[1] = CmsisDap::kPortDisabled;
                result = {CmsisDap::Status::Ok, 2};
            }
            else
            {
                result = g_dap.processPacket(g_swd, rx, rxLength, tx, sizeof(tx));
            }

            g_usb.finishCmsisDap(tx, result.status == CmsisDap::Status::Ok
                                   ? result.responseLength
                                   : 0);

            if (wireMode == WireMode::None && g_dap.connected())
                wireMode = WireMode::Swd;
            else if (wireMode == WireMode::Swd && !g_dap.connected())
                wireMode = releaseWire(wireMode, WireMode::Swd);

            if (wireMode == WireMode::Swd) lastActivityMs = Time::millis();
            continue;
        }

        if (!canUseWire(wireMode, WireMode::Jtag))
        {
            // wireMode == Swd. Preempt a stale SWD owner (e.g. OpenOCD exited
            // without DAP_Disconnect) once it has been idle past the threshold;
            // otherwise consume the packet without a reply so a live SWD session
            // is never stolen (DirtyJTAG has no BUSY response).
            if ((uint32_t)(Time::millis() - lastActivityMs) > kIdleReleaseMs)
            {
                g_dap.resetConnection(g_swd);
                wireMode = WireMode::None;
            }
            else
            {
                g_usb.finish(nullptr, 0);
                continue;
            }
        }

        if (wireMode == WireMode::None)
        {
            g_jtag.activate();
            wireMode = WireMode::Jtag;
        }

        const DirtyJtag::Result result =
            DirtyJtag::processPacket(g_jtag, rx, rxLength, tx, sizeof(tx));

        // DirtyJTAG has no wire-level error response. Malformed packets are
        // completed without a reply so the endpoint cannot wedge permanently.
        const size_t responseLength = result.status == DirtyJtag::Status::Ok
                                    ? result.responseLength
                                    : 0;
        g_usb.finish(tx, responseLength);
        lastActivityMs = Time::millis(); // JTAG owner activity

        if (result.releaseRequested)
        {
            g_jtag.disconnect();
            wireMode = releaseWire(wireMode, WireMode::Jtag);
        }
    }
}
