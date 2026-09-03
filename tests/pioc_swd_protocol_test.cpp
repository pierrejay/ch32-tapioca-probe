#include <assert.h>
#include <stdint.h>

#include "swd/pioc_swd_protocol.hpp"

int main()
{
    for (uint8_t request = 0; request < 16; ++request)
    {
        const uint8_t frame = PiocSwdProtocol::requestFrame(request);
        assert((frame & 0x01u) != 0); // start
        assert(((frame >> 1u) & 0x0fu) == request);
        assert((frame & 0x40u) == 0); // stop
        assert((frame & 0x80u) != 0); // park
        assert(((frame >> 5u) & 1u) == PiocSwdProtocol::parity4(request));
    }

    // DP IDCODE read: request nibble 0b0010, frame = 1 0 1 0 0 1 0 1.
    assert(PiocSwdProtocol::requestFrame(0x02) == 0xa5);

    // AP DRW read: APnDP=1, RnW=1, A2=A3=1.
    assert(PiocSwdProtocol::requestFrame(0x0f) == 0x9f);

    assert(PiocSwdProtocol::parity32(0x00000000u) == 0);
    assert(PiocSwdProtocol::parity32(0x00000001u) == 1);
    assert(PiocSwdProtocol::parity32(0x410fd214u) == 0);
    assert(PiocSwdProtocol::parity32(0xffffffffu) == 0);

    assert(PiocSwdProtocol::validAck(1));
    assert(PiocSwdProtocol::validAck(2));
    assert(PiocSwdProtocol::validAck(4));
    assert(PiocSwdProtocol::validAck(7));
    assert(!PiocSwdProtocol::validAck(0));
    assert(!PiocSwdProtocol::validAck(3));
    assert(!PiocSwdProtocol::validAck(5));
    assert(!PiocSwdProtocol::validAck(6));
    return 0;
}
