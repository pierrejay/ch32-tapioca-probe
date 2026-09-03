#include "ch32_sdk.hpp"
#include "ch32_jtag.hpp"
#include "ch32_pioc_swd.hpp"
#include "cmsis_dap.hpp"
#include "time.hpp"
#include "board_control.hpp"
#ifdef UART_BRIDGE
#include "uart_bridge.hpp"
#endif
#include "usb_cmsis_dap.hpp"

static Ch32PiocSwd g_swd;
static Ch32Jtag g_jtag;
static CmsisDap::Core g_dap;
static UsbCmsisDap g_usb;
#ifdef UART_BRIDGE
static UartBridge g_uart;
static void requestUartRxDrain() { g_usb.requestUartRxDrain(); }
#endif

int main(void)
{
    SystemInit();
    Time::init();
    BoardControl::init();
#ifdef UART_BRIDGE
    g_uart.init();
#endif

    g_swd.init();
    g_jtag.init();
#ifdef UART_BRIDGE
    g_usb.init(&g_uart);
    Time::setTickHandler(requestUartRxDrain);
#else
    g_usb.init(nullptr);
#endif

    uint8_t rx[UsbCmsisDap::kPacketSize];
    uint8_t tx[UsbCmsisDap::kPacketSize];

    while (1)
    {
        ActivityLed::tick();
#ifdef UART_BRIDGE
        if (g_usb.pollCdc()) ActivityLed::notify();
#endif

        if (g_usb.takeSessionReset()) g_dap.resetConnection(g_swd);

        size_t rxLength = 0;
        if (!g_usb.takeNextPacket(rx, rxLength)) continue;
        ActivityLed::notify();

        const CmsisDap::Result result =
            g_dap.processPacket(g_swd, &g_jtag, rx, rxLength, tx, sizeof(tx));
        g_usb.finish(tx, result.status == CmsisDap::Status::Ok
                           ? result.responseLength
                           : 0);
    }
}
