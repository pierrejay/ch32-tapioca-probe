#pragma once

#include <stdint.h>

// Pure, hardware-independent codec for the WCH RVSWD (two-wire) 52-bit DMI frame.
// Kept free of any CH32 SDK dependency so it is unit-testable natively against the
// real WCH-LinkE -> CH32V307 capture (see tests/wch_rvswd_frame_test.cpp and
// docs/wch-rvswd-protocol.md). The PIOC engine is a dumb shift register: C++ owns
// all framing, parity and packing; the blob only clocks HostBits out and TargBits in.
//
// Frame bits are numbered 0..51 and transmitted MSB-first (bit 0 first):
//   0..6   addr (a6..a0)      7   DM register
//   7      op                 1   0 = read, 1 = write
//   8      parity_host        1   XOR(addr bits, op)
//   9      park               1   = 1
//   10..13 padding            4   = 0b0101 (target-ignored on writes)
//   14..45 data               32  host-driven on write / target-driven on read
//   46     parity_data        1   XOR(data)
//   47     park               1   = 1
//   48..49 status             2   target: 1 ok, 3 busy, 2 fail
//   50..51 padding-target     2   don't-care
//
// Turnaround: READ  -> host drives [0:14), target drives [14:52)  (14 / 38 bits)
//             WRITE -> host drives [0:48), target drives [48:52)  (48 /  4 bits)

namespace WchLink
{
namespace Rvswd
{

constexpr int kFrameBits = 52;
constexpr int kReadHostBits = 14;
constexpr int kReadTargBits = 38;
constexpr int kWriteHostBits = 48;
constexpr int kWriteTargBits = 4;
constexpr int kFrameBytes = 7; // 56-bit capacity holds the 52-bit frame

// Even parity (XOR of all set bits) of the low bits of v.
inline uint8_t xorBits(uint32_t v)
{
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return static_cast<uint8_t>(v & 1u);
}

// Bit n of a frame buffer, MSB-first: frame bit n lives at byte n/8, bit (7 - n%8),
// so the first transmitted bit is the MSB of byte 0 (matches the PIOC shift order).
inline void setBit(uint8_t* buf, int n, uint8_t v)
{
    const uint8_t mask = static_cast<uint8_t>(1u << (7 - (n & 7)));
    if (v)
        buf[n >> 3] |= mask;
    else
        buf[n >> 3] = static_cast<uint8_t>(buf[n >> 3] & ~mask);
}

inline uint8_t getBit(const uint8_t* buf, int n)
{
    return static_cast<uint8_t>((buf[n >> 3] >> (7 - (n & 7))) & 1u);
}

inline uint32_t getField(const uint8_t* buf, int pos, int width)
{
    uint32_t v = 0;
    for (int i = 0; i < width; ++i)
        v = (v << 1) | getBit(buf, pos + i);
    return v;
}

// The host-driven portion of a frame, ready for the PIOC mailbox.
struct HostFrame
{
    uint8_t bytes[kFrameBytes]; // frame bit n at bytes[n/8], MSB-first
    uint8_t hostBits;           // bits the blob drives before turnaround
    uint8_t targBits;           // bits the blob samples after turnaround
};

inline uint8_t parityHost(uint8_t addr, uint8_t op)
{
    return static_cast<uint8_t>(xorBits(static_cast<uint32_t>(addr & 0x7f)) ^ (op & 1u));
}

// Fixed-geometry byte construction (MSB-first): the RVSWD frame layout is constant,
// so the host bytes are built directly with shifts/masks - this is the per-op hot
// path. Byte-exact; validated by tests/wch_rvswd_frame_test.cpp. Layout: [0:7]addr
// [7]op [8]parity [9]park=1 [10:14]pad=0b0101 [14:46]data [46]parity_data [47]park=1.
inline HostFrame packWrite(uint8_t addr, uint32_t data)
{
    HostFrame f;
    const uint8_t ph = parityHost(addr, 1);
    const uint8_t pd = xorBits(data);
    f.bytes[0] = static_cast<uint8_t>(((addr & 0x7f) << 1) | 1u);              // addr, op=1
    f.bytes[1] = static_cast<uint8_t>((ph << 7) | (1u << 6) | (0x5u << 2)      // parity,park,pad
                                      | ((data >> 30) & 0x3u));                 // data[31:30]
    f.bytes[2] = static_cast<uint8_t>((data >> 22) & 0xffu);                   // data[29:22]
    f.bytes[3] = static_cast<uint8_t>((data >> 14) & 0xffu);                   // data[21:14]
    f.bytes[4] = static_cast<uint8_t>((data >> 6) & 0xffu);                    // data[13:6]
    f.bytes[5] = static_cast<uint8_t>(((data & 0x3fu) << 2) | (pd << 1) | 1u); // data[5:0],pd,park
    f.bytes[6] = 0;
    f.hostBits = kWriteHostBits;
    f.targBits = kWriteTargBits;
    return f;
}

inline HostFrame packRead(uint8_t addr)
{
    HostFrame f;
    const uint8_t ph = parityHost(addr, 0);
    f.bytes[0] = static_cast<uint8_t>((addr & 0x7f) << 1);                     // addr, op=0
    f.bytes[1] = static_cast<uint8_t>((ph << 7) | (1u << 6) | (0x5u << 2));    // parity,park,pad
    f.bytes[2] = 0;
    f.bytes[3] = 0;
    f.bytes[4] = 0;
    f.bytes[5] = 0;
    f.bytes[6] = 0;
    f.hostBits = kReadHostBits;
    f.targBits = kReadTargBits;
    return f;
}

// Target reply. `targ` holds the sampled bits MSB-first from the turnaround point:
// targ bit j == frame bit (hostBits + j).
struct ReadReply
{
    uint32_t data;
    uint8_t status;  // raw 2-bit target status
    bool parityOk;   // received parity_data matches XOR(data)
};

inline ReadReply unpackRead(const uint8_t* targ)
{
    // Read turnaround at bit 14: targ bit 0 == frame bit 14, so the 32 data bits are
    // byte-aligned in targ[0..3] (MSB-first); parity/status fall in targ[4].
    ReadReply r;
    r.data = (static_cast<uint32_t>(targ[0]) << 24) |
             (static_cast<uint32_t>(targ[1]) << 16) |
             (static_cast<uint32_t>(targ[2]) << 8) |
             static_cast<uint32_t>(targ[3]);
    const uint8_t pdata = static_cast<uint8_t>((targ[4] >> 7) & 1u);  // frame 46
    r.status = static_cast<uint8_t>((targ[4] >> 4) & 3u);            // frame 48..49
    r.parityOk = (pdata == xorBits(r.data));
    return r;
}

inline uint8_t unpackWriteStatus(const uint8_t* targ)
{
    // Write turnaround at bit 48: targ bit 0 == frame bit 48 (status high bit).
    return static_cast<uint8_t>((targ[0] >> 6) & 3u);
}

// Raw target status meanings (see capture): 1 = ok, 0 = ok/no-change, 3 = busy, 2 = fail.
enum : uint8_t { kStatusOk = 1, kStatusOkAlt = 0, kStatusFail = 2, kStatusBusy = 3 };

} // namespace Rvswd
} // namespace WchLink
