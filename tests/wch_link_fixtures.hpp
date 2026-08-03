// wch_link_fixtures.hpp - request/reply fixtures for the WCH-Link (minichlink
// -C linke) command subset. Extracted byte-exact from minichlink/pgm-wch-linke.c
// (cnlohr/ch32fun). Full framing spec + source line citations:
// docs/wch-link-usb-protocol.md.
//
// Each contract records which reply fields minichlink uses. Ignored bytes remain
// placeholders until confirmed against a WCH-LinkE capture.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace WchLinkFixtures
{

// How strictly a fixture's `reply` bytes must be reproduced by the probe.
enum class ReplyContract : uint8_t
{
    ExactBytes,     // every reply byte is host-verified: reproduce exactly.
    LengthAndData,  // host verifies length + big-endian data (4..7) + status (8).
    LengthAndStatus,// host verifies reply length and the status byte only.
    NonEmptyOnly,   // host ignores content but blocks unless >=1 byte is returned.
};

struct Fixture
{
    const char*   name;
    const uint8_t* request;
    uint8_t        requestLen;
    const uint8_t* reply;      // canonical reply the probe should emit.
    uint8_t        replyLen;
    ReplyContract  contract;
};

// ---- identify: 82 0d 04 <maj> <min> <type=0x12 LinkE> 00 (type byte VERIFIED) --
inline constexpr uint8_t kIdentifyReq[]   = {0x81, 0x0d, 0x01, 0x01};
inline constexpr uint8_t kIdentifyReply[] = {0x82, 0x0d, 0x04, 0x02, 0x08, 0x12, 0x00};

// ---- stop / exit programming (reply content ignored, must be non-empty) --------
inline constexpr uint8_t kStopReq[]   = {0x81, 0x0d, 0x01, 0xff};
inline constexpr uint8_t kStopReply[] = {0x82, 0x0d, 0x01, 0xff}; // [I] placeholder

// ---- connect / detect (9-byte shape; must NOT be len==4 nor start 81 55 01) ----
inline constexpr uint8_t kConnectReq[]   = {0x81, 0x0d, 0x01, 0x02};
inline constexpr uint8_t kConnectReply[] = {0x82, 0x0d, 0x05, 0x09, 0x00, 0x30, 0x05, 0x00, 0x00}; // [I] shape

// ---- set family + interface speed (reply ignored, must be non-empty) -----------
inline constexpr uint8_t kSetSpeedReq[]   = {0x81, 0x0c, 0x02, 0x01, 0x02};
inline constexpr uint8_t kSetSpeedReply[] = {0x82, 0x0c, 0x01, 0x01}; // [I] placeholder

// ---- DMI read DMSTATUS(0x11): op=1, data bytes 0; reply data BE in 4..7 ---------
// Example reply data 0x00030382 is a plausible DMSTATUS; status byte 0x00 = OK.
inline constexpr uint8_t kDmiReadReq[]   = {0x81, 0x08, 0x06, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01};
inline constexpr uint8_t kDmiReadReply[] = {0x82, 0x08, 0x06, 0x11, 0x00, 0x03, 0x03, 0x82, 0x00};

// ---- DMI write DMCONTROL(0x10) = 0x80000001: op=2, data BE in 4..7 -------------
inline constexpr uint8_t kDmiWriteReq[]   = {0x81, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x02};
inline constexpr uint8_t kDmiWriteReply[] = {0x82, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x00};

// ---- DMI error: status byte 0x02 -> host takes the error branch ----------------
inline constexpr uint8_t kDmiErrorReq[]   = {0x81, 0x08, 0x06, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01};
inline constexpr uint8_t kDmiErrorReply[] = {0x82, 0x08, 0x06, 0x11, 0x00, 0x00, 0x00, 0x00, 0x02};

// ---- recovery: hold, and force-reset-low --------------------------------------
inline constexpr uint8_t kHoldReq[]   = {0x81, 0x0d, 0x01, 0x03};
inline constexpr uint8_t kHoldReply[] = {0x82, 0x0d, 0x05, 0x09, 0x00, 0x30, 0x05, 0x00, 0x00}; // [I]
inline constexpr uint8_t kResetLowReq[]   = {0x81, 0x0d, 0x01, 0x13};
inline constexpr uint8_t kResetLowReply[] = {0x82, 0x0d, 0x01, 0x13}; // [I] placeholder

#define WCHLINK_FIXTURE(id, contract) \
    { #id, id##Req, (uint8_t)sizeof(id##Req), id##Reply, (uint8_t)sizeof(id##Reply), contract }

inline constexpr Fixture kAll[] = {
    WCHLINK_FIXTURE(kIdentify, ReplyContract::ExactBytes),
    WCHLINK_FIXTURE(kStop,     ReplyContract::NonEmptyOnly),
    WCHLINK_FIXTURE(kConnect,  ReplyContract::LengthAndStatus),
    WCHLINK_FIXTURE(kSetSpeed, ReplyContract::NonEmptyOnly),
    WCHLINK_FIXTURE(kDmiRead,  ReplyContract::LengthAndData),
    WCHLINK_FIXTURE(kDmiWrite, ReplyContract::LengthAndData),
    WCHLINK_FIXTURE(kDmiError, ReplyContract::LengthAndStatus),
    WCHLINK_FIXTURE(kHold,     ReplyContract::NonEmptyOnly),
    WCHLINK_FIXTURE(kResetLow, ReplyContract::NonEmptyOnly),
};

inline constexpr size_t kCount = sizeof(kAll) / sizeof(kAll[0]);

// DMI reply field accessors (big-endian data in bytes 4..7, status in byte 8).
inline constexpr uint8_t kDmiReplyLen = 9;
inline constexpr uint8_t kDmiStatusError2 = 0x02;
inline constexpr uint8_t kDmiStatusError3 = 0x03;

inline constexpr uint32_t dmiReplyData(const uint8_t* reply)
{
    return ((uint32_t)reply[4] << 24) | ((uint32_t)reply[5] << 16) |
           ((uint32_t)reply[6] << 8) | (uint32_t)reply[7];
}

inline constexpr bool dmiReplyIsError(const uint8_t* reply, uint8_t len)
{
    return len != kDmiReplyLen || reply[8] == kDmiStatusError2 || reply[8] == kDmiStatusError3;
}

} // namespace WchLinkFixtures
