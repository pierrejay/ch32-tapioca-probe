#include "protocol.hpp"

namespace WchLink
{
namespace
{

// Reply framing constants (see docs/wch-link-usb-protocol.md).
constexpr uint8_t kCmdPrefix = 0x81;   // host -> probe
constexpr uint8_t kReplyPrefix = 0x82; // probe -> host

// Command families (rx[1]).
constexpr uint8_t kFamilyDmi = 0x08;   // 81 08 06 <reg> <d3..d0> <op>
constexpr uint8_t kFamilySpeed = 0x0c; // 81 0c 02 <family> <speed>
constexpr uint8_t kFamilyControl = 0x0d;
constexpr uint8_t kFamilyReset = 0x0b; // 81 0b 01 01 (seen in the reset dance)

// Control subcommands (rx[3]) for the 0x0d family.
constexpr uint8_t kCtrlIdentify = 0x01;
constexpr uint8_t kCtrlConnect = 0x02;
constexpr uint8_t kCtrlHold = 0x03;
constexpr uint8_t kCtrlResetLow = 0x13;
constexpr uint8_t kCtrlStop = 0xff;

// DMI operation codes (rx[8]).
constexpr uint8_t kDmiOpRead = 0x01;
constexpr uint8_t kDmiOpWrite = 0x02;

// Reply status bytes: 0x02/0x03 are errors to the host (pgm-wch-linke.c:310,330).
constexpr uint8_t kStatusOk = 0x00;
constexpr uint8_t kStatusBusy = 0x03;
constexpr uint8_t kStatusError = 0x02;

// Firmware version reported by identify. Host prints these but does not branch on
// them; the load-bearing field is the LinkE type byte.
constexpr uint8_t kFirmwareMajor = 0x02;
constexpr uint8_t kFirmwareMinor = 0x08;

constexpr uint8_t kDmiReplyLen = 9;

uint8_t statusByte(DmiStatus status)
{
    switch (status)
    {
        case DmiStatus::Ok: return kStatusOk;
        case DmiStatus::Busy: return kStatusBusy;
        default: return kStatusError;
    }
}

// Minimal non-empty acknowledgement for commands whose reply the host ignores.
// Preserves the "never stall EP1" invariant without inventing WCH payload bytes.
Result ack(uint8_t* tx, uint8_t family, Status status)
{
    tx[0] = kReplyPrefix;
    tx[1] = family;
    tx[2] = 0x01;
    tx[3] = kStatusOk;
    return {4, status};
}

void writeDmiReply(uint8_t* tx, uint8_t reg, uint32_t data, uint8_t status)
{
    tx[0] = kReplyPrefix;
    tx[1] = kFamilyDmi;
    tx[2] = 0x06;
    tx[3] = reg;
    tx[4] = static_cast<uint8_t>(data >> 24);
    tx[5] = static_cast<uint8_t>(data >> 16);
    tx[6] = static_cast<uint8_t>(data >> 8);
    tx[7] = static_cast<uint8_t>(data);
    tx[8] = status;
}

// Identity reply: 82 0d 04 <maj> <min> <type> 00 (7 bytes).
Result buildIdentity(uint8_t* tx)
{
    tx[0] = kReplyPrefix;
    tx[1] = kFamilyControl;
    tx[2] = 0x04;
    tx[3] = kFirmwareMajor;
    tx[4] = kFirmwareMinor;
    tx[5] = kProgrammerTypeLinkE;
    tx[6] = 0x00;
    return {7, Status::Ok};
}

// Connect/detect reply: a 9-byte-shaped packet whose status byte (8) is not an
// error and which does not collide with the host retry sentinels (len 4 / prefix
// 81 55 01). Chip-id bytes are host-ignored under FORCE_EXTERNAL_CHIP_DETECTION.
Result buildConnectReply(uint8_t* tx, bool present)
{
    tx[0] = kReplyPrefix;
    tx[1] = kFamilyControl;
    tx[2] = 0x05;
    tx[3] = 0x09;
    tx[4] = 0x00;
    tx[5] = 0x30;
    tx[6] = 0x05;
    tx[7] = 0x00;
    tx[8] = present ? kStatusOk : kStatusError;
    return {kDmiReplyLen, Status::Ok};
}

} // namespace

void Core::reset(IDmi& port)
{
    if (connected_) port.disconnect();
    connected_ = false;
}

Result Core::processPacket(IDmi& port, const uint8_t* rx, size_t rxLength,
                           uint8_t* tx, size_t txCapacity)
{
    // Every reply here is <= 9 bytes; a 64-byte EP buffer always fits. Guard
    // defensively so a caller with a tiny buffer still gets a bounded reply.
    if (txCapacity < kDmiReplyLen || rxLength < 2 || rx[0] != kCmdPrefix)
        return ack(tx, 0x00, Status::UnsupportedCommand);

    switch (rx[1])
    {
        case kFamilyDmi:
        {
            // 81 08 06 <reg> <d3 d2 d1 d0> <op>
            if (rxLength < kDmiReplyLen)
            {
                // Bounded error reply without touching the wire on a malformed op.
                writeDmiReply(tx, 0x00, 0, kStatusError);
                return {kDmiReplyLen, Status::TruncatedCommand};
            }

            const uint8_t reg = rx[3];
            const uint32_t data = (static_cast<uint32_t>(rx[4]) << 24) |
                                  (static_cast<uint32_t>(rx[5]) << 16) |
                                  (static_cast<uint32_t>(rx[6]) << 8) |
                                  static_cast<uint32_t>(rx[7]);
            const uint8_t op = rx[8];

            if (op == kDmiOpRead)
            {
                uint32_t value = 0;
                const DmiStatus st = port.readDmi(reg, value);
                writeDmiReply(tx, reg, value, statusByte(st));
                return {kDmiReplyLen, Status::Ok};
            }
            if (op == kDmiOpWrite)
            {
                const DmiStatus st = port.writeDmi(reg, data);
                // Echo the written data back, as a genuine LinkE does.
                writeDmiReply(tx, reg, data, statusByte(st));
                return {kDmiReplyLen, Status::Ok};
            }
            writeDmiReply(tx, reg, 0, kStatusError);
            return {kDmiReplyLen, Status::UnsupportedCommand};
        }

        case kFamilySpeed: // 81 0c 02 <family> <speed>: accept, host ignores reply.
            return ack(tx, kFamilySpeed, Status::Ok);

        case kFamilyReset: // 81 0b 01 01
            return ack(tx, kFamilyReset, Status::Ok);

        case kFamilyControl:
        {
            if (rxLength < 4)
                return ack(tx, kFamilyControl, Status::TruncatedCommand);

            switch (rx[3])
            {
                case kCtrlIdentify:
                    // Reset probe-side session state and report version/type.
                    reset(port);
                    return buildIdentity(tx);

                case kCtrlConnect:
                    connected_ = port.connect();
                    return buildConnectReply(tx, connected_);

                case kCtrlStop:
                    reset(port);
                    return ack(tx, kFamilyControl, Status::Ok);

                case kCtrlHold:
                case kCtrlResetLow:
                    // Recovery acks; the physical recovery policy lives in the
                    // port/target layer once the wire engine exists.
                    return ack(tx, kFamilyControl, Status::Ok);

                default:
                    return ack(tx, kFamilyControl, Status::UnsupportedCommand);
            }
        }

        default:
            return ack(tx, rx[1], Status::UnsupportedCommand);
    }
}

} // namespace WchLink
