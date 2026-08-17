// Self-consistency checks for WCH-Link request/reply fixtures.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "wch_link_fixtures.hpp"

namespace F = WchLinkFixtures;

int main()
{
    // Every fixture is well-formed: named, non-empty request, WCH command prefix.
    for (size_t i = 0; i < F::kCount; ++i)
    {
        const F::Fixture& f = F::kAll[i];
        assert(f.name != nullptr);
        assert(f.request != nullptr && f.requestLen >= 3);
        assert(f.reply != nullptr && f.replyLen >= 1);   // never a silent command
        assert(f.request[0] == 0x81);                    // host->probe command prefix
        assert(f.reply[0] == 0x82);                      // probe->host reply prefix
        assert(f.replyLen == (size_t)f.reply[2] + 3);    // LEN describes payload
    }

    // Identity: type byte (offset 5) is 0x12 == 18 == WCH-LinkE, the one field the
    // host branches the programmer name on.
    assert(F::kIdentifyReply[5] == 0x12);
    assert(sizeof(F::kIdentifyReply) == 7);

    // DMI requests are 9 bytes: 81 08 06 <reg> <d3 d2 d1 d0> <op>.
    assert(sizeof(F::kDmiReadReq) == 9 && F::kDmiReadReq[2] == 0x06);
    assert(F::kDmiReadReq[8] == 0x01);  // op = read
    assert(F::kDmiWriteReq[8] == 0x02); // op = write

    // Write request carries big-endian data 0x80000001 in bytes 4..7.
    assert(F::dmiReplyData(F::kDmiWriteReq) == 0x80000001u);

    // DMI read reply decodes big-endian data from bytes 4..7 and is not an error.
    assert(F::dmiReplyData(F::kDmiReadReply) == 0x00030382u);
    assert(!F::dmiReplyIsError(F::kDmiReadReply, sizeof(F::kDmiReadReply)));

    // The error fixture is actually flagged as an error by the host's rule.
    assert(F::dmiReplyIsError(F::kDmiErrorReply, sizeof(F::kDmiErrorReply)));

    // Every LengthAndData / LengthAndStatus fixture is exactly the 9-byte DMI
    // shape the host length-checks.
    for (size_t i = 0; i < F::kCount; ++i)
    {
        const F::Fixture& f = F::kAll[i];
        if (f.contract == F::ReplyContract::LengthAndData ||
            f.contract == F::ReplyContract::LengthAndStatus)
            assert(f.replyLen == F::kDmiReplyLen);
    }

    // Connect reply must not collide with the host's retry sentinels: length != 4,
    // and it must not start with 81 55 01 (pgm-wch-linke.c:371,436).
    assert(sizeof(F::kConnectReply) != 4);
    assert(!(F::kConnectReply[0] == 0x81 && F::kConnectReply[1] == 0x55 &&
             F::kConnectReply[2] == 0x01));

    printf("wch_link_fixtures_test: %zu fixtures OK\n", F::kCount);
    return 0;
}
