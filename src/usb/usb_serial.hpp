#pragma once

#include <stdint.h>

// Shared USB serial-number string, derived from the CH32X035 factory unique ID so
// two probes on the same host enumerate with distinct serials (host tools like
// OpenOCD / probe-rs can then pin a session to one). Both firmwares use it.
//
// The UID lives in the read-only factory info block (CH32X035 reference manual,
// R32_ESIG_UNIID1..3):
//   0x1FFFF7E8  UNIID1
//   0x1FFFF7EC  UNIID2
//   0x1FFFF7F0  UNIID3
// UNIID1+UNIID2 (the low 64 bits) carry the per-die value; UNIID3 reads 0xFFFFFFFF
// on this part (reserved / not populated), so it is dropped. We emit the low 64 bits
// as 16 uppercase hex chars in a USB STRING descriptor (UTF-16LE).
namespace UsbSerial
{

constexpr uint32_t kUniqueIdBase = 0x1FFFF7E8; // R32_ESIG_UNIID1
constexpr int kUidWords = 2;                   // low 64 bits (UNIID1+UNIID2)
constexpr int kHexChars = kUidWords * 8;       // 16 nibbles

// Returns a static USB string descriptor holding the UID as hex. Built once on the
// first call (idempotent); the pointer stays valid for the whole session, so it is
// safe to hand to the multi-packet EP0 descriptor cursor.
inline const uint8_t* serialDescriptor()
{
    static uint8_t desc[2 + kHexChars * 2];
    static bool built = false;
    if (!built)
    {
        static const char hex[] = "0123456789ABCDEF";
        const volatile uint32_t* uid =
            reinterpret_cast<const volatile uint32_t*>(kUniqueIdBase);
        desc[0] = static_cast<uint8_t>(sizeof(desc)); // bLength
        desc[1] = 0x03;                               // bDescriptorType = STRING
        int p = 2;
        for (int w = 0; w < kUidWords; ++w)
        {
            const uint32_t v = uid[w];
            for (int nib = 7; nib >= 0; --nib)
            {
                desc[p++] = static_cast<uint8_t>(hex[(v >> (nib * 4)) & 0xF]);
                desc[p++] = 0x00; // UTF-16LE high byte
            }
        }
        built = true;
    }
    return desc;
}

} // namespace UsbSerial
