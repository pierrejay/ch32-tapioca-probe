// wch_rvswd_frame_test.cpp - validates the RVSWD 52-bit frame codec
// (src/wchlink/rvswd_frame.hpp) against golden frames captured bit-exact from a
// real WCH-LinkE -> CH32V307 exchange (decoded with perigoso/sigrok-rvswd).
//
// Build:
//   c++ -std=c++17 -Wall -Wextra -Isrc -Itests tests/wch_rvswd_frame_test.cpp
//   ./tests/wch_rvswd_frame_test
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wchlink/rvswd_frame.hpp"

namespace R = WchLink::Rvswd;

// Golden frames: {rw, addr, data, status, 52 raw bits (MSB-first, as transmitted)}.
struct Golden
{
    char rw;
    uint8_t addr;
    uint32_t data;
    uint8_t status;
    const char* bits;
};

static const Golden kGolden[] = {
    {'R', 0x11, 0x00000c82, 1, "0010001001010100000000000000000000110010000010010111"},
    {'R', 0x04, 0x30700568, 1, "0000100011010100110000011100000000010101101000010111"},
    {'W', 0x10, 0x40000001, 1, "0010000101000101000000000000000000000000000001010111"},
    {'W', 0x05, 0x1ffff704, 1, "0000101111010100011111111111111111011100000100110111"},
    {'W', 0x7d, 0x5aa50400, 1, "1111101111010101011010101001010000010000000000110111"},
    {'W', 0x17, 0x02200000, 1, "0010111111010100000010001000000000000000000000010111"},
};

// Pack the ASCII bit string [from, to) into a fresh MSB-first buffer at position 0.
static void sliceBits(const char* bits, int from, int to, uint8_t* out)
{
    memset(out, 0, R::kFrameBytes);
    for (int i = 0; i < (to - from); ++i)
        R::setBit(out, i, bits[from + i] == '1' ? 1 : 0);
}

static void buildFullFrame(const char* bits, uint8_t* out)
{
    sliceBits(bits, 0, R::kFrameBits, out);
}

int main()
{
    int checked = 0;
    for (const Golden& g : kGolden)
    {
        uint8_t full[R::kFrameBytes];
        buildFullFrame(g.bits, full);

        // Decoded fields must match the captured values.
        assert(R::getField(full, 0, 7) == g.addr);
        assert(R::getBit(full, 7) == (g.rw == 'W' ? 1 : 0));
        assert(R::getField(full, 14, 32) == g.data);
        assert(R::getField(full, 48, 2) == g.status);
        // Parity invariants hold on the real bits.
        assert(R::getBit(full, 8) == R::parityHost(g.addr, g.rw == 'W' ? 1 : 0));
        assert(R::getBit(full, 46) == R::xorBits(g.data));

        if (g.rw == 'R')
        {
            // Our packed read must reproduce the host-driven [0:14) bits exactly
            // (reads are deterministic: padding 0x5, park 1 - 59/59 in the capture).
            R::HostFrame f = R::packRead(g.addr);
            assert(f.hostBits == R::kReadHostBits);
            assert(f.targBits == R::kReadTargBits);
            for (int i = 0; i < R::kReadHostBits; ++i)
                assert(R::getBit(f.bytes, i) == R::getBit(full, i));

            // Unpack the target reply: targ bit j == frame bit (14 + j).
            uint8_t targ[R::kFrameBytes];
            sliceBits(g.bits, R::kReadHostBits, R::kFrameBits, targ);
            R::ReadReply rep = R::unpackRead(targ);
            assert(rep.data == g.data);
            assert(rep.status == g.status);
            assert(rep.parityOk);
        }
        else
        {
            // Writes: park/padding are target-ignored and the WCH-LinkE drives them
            // non-canonically, so compare only the deterministic fields.
            R::HostFrame f = R::packWrite(g.addr, g.data);
            assert(f.hostBits == R::kWriteHostBits);
            assert(f.targBits == R::kWriteTargBits);
            assert(R::getField(f.bytes, 0, 7) == g.addr);   // addr
            assert(R::getBit(f.bytes, 7) == 1);             // op = write
            assert(R::getBit(f.bytes, 8) == R::getBit(full, 8)); // parity_host
            assert(R::getField(f.bytes, 14, 32) == g.data); // data
            assert(R::getBit(f.bytes, 46) == R::getBit(full, 46)); // parity_data

            // Unpack the write status: targ bit 0 == frame bit 48.
            uint8_t targ[R::kFrameBytes];
            sliceBits(g.bits, R::kWriteHostBits, R::kFrameBits, targ);
            assert(R::unpackWriteStatus(targ) == g.status);
        }
        ++checked;
    }

    // Round-trip: pack a write, decode its own fields back.
    {
        R::HostFrame f = R::packWrite(0x10, 0x80000001);
        assert(R::getField(f.bytes, 0, 7) == 0x10);
        assert(R::getField(f.bytes, 14, 32) == 0x80000001u);
        assert(R::getBit(f.bytes, 46) == R::xorBits(0x80000001u));
        assert(R::getBit(f.bytes, 8) == R::parityHost(0x10, 1));
    }

    // Canonical read frame shape.
    {
        R::HostFrame f = R::packRead(0x11);
        assert(R::getBit(f.bytes, 7) == 0);
        assert(R::getBit(f.bytes, 9) == 1);            // park
        assert(R::getField(f.bytes, 10, 4) == 0x5);    // padding
    }

    printf("wch_rvswd_frame_test: OK (%d golden frames + round-trips)\n", checked);
    return 0;
}
