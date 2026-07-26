#include <assert.h>
#include <stdint.h>

#include "usb/packet_order.hpp"

int main()
{
    assert(packetArrivedBefore(1, 2));
    assert(!packetArrivedBefore(2, 1));
    assert(!packetArrivedBefore(7, 7));

    // The IRQ counter may wrap during a very long uptime.
    assert(packetArrivedBefore(UINT32_MAX, 0));
    assert(packetArrivedBefore(UINT32_MAX - 1, 1));
    assert(!packetArrivedBefore(0, UINT32_MAX));
    return 0;
}
