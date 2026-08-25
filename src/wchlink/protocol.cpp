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
constexpr uint8_t kFamilyDiagnostics = 0x7f; // Tapioca-private, never sent by WCH hosts.

// Control subcommands (rx[3]) for the 0x0d family.
constexpr uint8_t kCtrlIdentify = 0x01;
constexpr uint8_t kCtrlConnect = 0x02;
constexpr uint8_t kCtrlHold = 0x03;
constexpr uint8_t kCtrlResetLow = 0x13;
constexpr uint8_t kCtrlStop = 0xff;

constexpr uint8_t kDiagQuery = 0x00;
constexpr uint8_t kDiagClear = 0x01;

// DMI operation codes (rx[8]).
constexpr uint8_t kDmiOpRead = 0x01;
constexpr uint8_t kDmiOpWrite = 0x02;
constexpr uint8_t kDmiChipId = 0x7f;

constexpr uint8_t kChipFamilyV10x = 0x01;
constexpr uint8_t kChipFamilyV20x = 0x05;
constexpr uint8_t kChipFamilyV30x = 0x06;
constexpr uint8_t kChipFamilyV003 = 0x09;
constexpr uint8_t kChipFamilyCh643 = 0x0c;
constexpr uint8_t kChipFamilyX03x = 0x0d;
constexpr uint8_t kChipFamilyL103 = 0x0e;
constexpr uint8_t kChipFamilyCh641 = 0x49;
constexpr uint8_t kChipFamilyV00x = 0x4e;
constexpr uint8_t kChipFamilyV317 = 0x86;
constexpr uint8_t kChipFamilyH41x = 0xc6;
constexpr uint8_t kChipFamilyV205 = 0xce;

// Reply status bytes: 0x02/0x03 are errors to the host (pgm-wch-linke.c:310,330).
constexpr uint8_t kStatusOk = 0x00;
constexpr uint8_t kStatusBusy = 0x03;
constexpr uint8_t kStatusError = 0x02;

// Firmware version reported by identify. Host prints these but does not branch on
// them; the load-bearing field is the LinkE type byte.
constexpr uint8_t kFirmwareMajor = 0x02;
constexpr uint8_t kFirmwareMinor = 0x08;

constexpr uint8_t kDmiReplyLen = 9;
constexpr size_t kDiagnosticsReplyLen = 43;

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

void writeU32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value >> 24);
    out[1] = static_cast<uint8_t>(value >> 16);
    out[2] = static_cast<uint8_t>(value >> 8);
    out[3] = static_cast<uint8_t>(value);
}

Result buildDiagnostics(IDmi& port, uint8_t* tx)
{
    DmiDiagnostics diagnostics;
    const bool supported = port.getDiagnostics(diagnostics);

    tx[0] = kReplyPrefix;
    tx[1] = kFamilyDiagnostics;
    tx[2] = static_cast<uint8_t>(kDiagnosticsReplyLen - 3);
    tx[3] = 0x01; // diagnostics format version
    tx[4] = supported ? 1 : 0;
    tx[5] = diagnostics.valid ? 1 : 0;
    tx[6] = static_cast<uint8_t>(diagnostics.transport);
    tx[7] = static_cast<uint8_t>(diagnostics.operation);
    tx[8] = diagnostics.address;
    tx[9] = static_cast<uint8_t>(diagnostics.status);
    tx[10] = diagnostics.rawStatus;
    tx[11] = diagnostics.receivedParity;
    tx[12] = diagnostics.expectedParity;
    tx[13] = diagnostics.rawLength;
    for (size_t i = 0; i < sizeof(diagnostics.raw); ++i)
        tx[14 + i] = diagnostics.raw[i];
    writeU32(tx + 19, diagnostics.data);
    writeU32(tx + 23, diagnostics.wireFrames);
    writeU32(tx + 27, diagnostics.busyReplies);
    writeU32(tx + 31, diagnostics.targetFaults);
    writeU32(tx + 35, diagnostics.parityErrors);
    writeU32(tx + 39, diagnostics.engineTimeouts);
    return {kDiagnosticsReplyLen, Status::Ok};
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

bool classifyChip(uint32_t chipId, uint8_t& family)
{
    switch ((chipId >> 20) & 0x0fffu)
    {
        case 0x002:
        case 0x004:
        case 0x005:
        case 0x006:
        case 0x007: family = kChipFamilyV00x; return true;
        case 0x003: family = kChipFamilyV003; return true;
        case 0x033:
        case 0x035: family = kChipFamilyX03x; return true;
        case 0x103: family = kChipFamilyL103; return true;
        case 0x203:
        case 0x208: family = kChipFamilyV20x; return true;
        case 0x205: family = kChipFamilyV205; return true;
        case 0x250: family = kChipFamilyV10x; return true;
        case 0x303:
        case 0x305:
        case 0x307: family = kChipFamilyV30x; return true;
        case 0x317: family = kChipFamilyV317; return true;
        case 0x415:
        case 0x416:
        case 0x417: family = kChipFamilyH41x; return true;
        case 0x641: family = kChipFamilyCh641; return true;
        case 0x643: family = kChipFamilyCh643; return true;
        default: return false;
    }
}

// Connect/detect reply: family followed by the four-byte chip ID read from the
// target's WCH-specific DMI register.
Result buildConnectReply(uint8_t* tx, uint8_t family, uint32_t chipId)
{
    tx[0] = kReplyPrefix;
    tx[1] = kFamilyControl;
    tx[2] = 0x05;
    tx[3] = family;
    tx[4] = static_cast<uint8_t>(chipId >> 24);
    tx[5] = static_cast<uint8_t>(chipId >> 16);
    tx[6] = static_cast<uint8_t>(chipId >> 8);
    tx[7] = static_cast<uint8_t>(chipId);
    return {8, Status::Ok};
}

Result buildConnectError(uint8_t* tx)
{
    // WCH-Link failure framing recognized by minichlink's retry path and by
    // probe-rs's protocol-error parser.
    tx[0] = kCmdPrefix;
    tx[1] = 0x55;
    tx[2] = 0x01;
    tx[3] = 0x01;
    return {4, Status::TargetUnavailable};
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
    if (tx == nullptr || txCapacity < 4)
        return {0, Status::UnsupportedCommand};

    // Four bytes are enough for a bounded error reply; command replies need nine.
    if (rx == nullptr || txCapacity < kDmiReplyLen ||
        rxLength < 2 || rx[0] != kCmdPrefix)
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

        case kFamilyDiagnostics:
            if (rxLength < 4)
                return ack(tx, kFamilyDiagnostics, Status::TruncatedCommand);
            if (rx[3] == kDiagClear)
            {
                port.clearDiagnostics();
                return ack(tx, kFamilyDiagnostics, Status::Ok);
            }
            if (rx[3] == kDiagQuery && txCapacity >= kDiagnosticsReplyLen)
                return buildDiagnostics(port, tx);
            return ack(tx, kFamilyDiagnostics, Status::UnsupportedCommand);

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
                {
                    connected_ = port.connect();
                    uint32_t chipId = 0;
                    uint8_t family = 0;
                    if (!connected_ ||
                        port.readDmi(kDmiChipId, chipId) != DmiStatus::Ok ||
                        !classifyChip(chipId, family))
                    {
                        if (connected_) port.disconnect();
                        connected_ = false;
                        return buildConnectError(tx);
                    }
                    // Start the post-attach capture from a known clean point; if
                    // attachment itself fails, its evidence remains available.
                    port.clearDiagnostics();
                    return buildConnectReply(tx, family, chipId);
                }

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
