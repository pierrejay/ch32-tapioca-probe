// DirtyJTAG v2 command core, adapted from dirtyjtag/DirtyJTAG (MIT).
// This version is transport-independent and bounds-checks every command.
#include "protocol.hpp"

#include <string.h>

namespace DirtyJtag
{
namespace
{

enum Command : uint8_t
{
    Stop   = 0x00,
    Info   = 0x01,
    Freq   = 0x02,
    Xfer   = 0x03,
    SetSig = 0x04,
    GetSig = 0x05,
    Clock  = 0x06,
    Release = 0x07,
};

constexpr uint8_t kCommandMask = 0x0F;
constexpr uint8_t kNoRead = 0x80;
constexpr uint8_t kExtendLength = 0x40;
constexpr uint8_t kReadout = 0x80;

enum Signal : uint8_t
{
    SigTck  = 1u << 1,
    SigTdi  = 1u << 2,
    SigTdo  = 1u << 3,
    SigTms  = 1u << 4,
    SigTrst = 1u << 5,
    SigSrst = 1u << 6,
};

bool require(size_t cursor, size_t needed, size_t length)
{
    return cursor <= length && needed <= (length - cursor);
}

bool reserve(size_t used, size_t needed, size_t capacity)
{
    return used <= capacity && needed <= (capacity - used);
}

} // namespace

Result processPacket(IJtag& jtag,
                     const uint8_t* rx,
                     size_t rxLength,
                     uint8_t* tx,
                     size_t txCapacity)
{
    Result result;
    size_t cursor = 0;

    while (cursor < rxLength)
    {
        const uint8_t commandByte = rx[cursor];
        const uint8_t command = commandByte & kCommandMask;
        if (command == Stop) break;

        switch (command)
        {
            case Info:
            {
                static constexpr uint8_t info[10] =
                    {'D', 'J', 'T', 'A', 'G', '2', '\n', 0, 0, 0};
                if (!reserve(result.responseLength, sizeof(info), txCapacity))
                {
                    result.status = Status::ResponseOverflow;
                    return result;
                }
                memcpy(tx + result.responseLength, info, sizeof(info));
                result.responseLength += sizeof(info);
                cursor += 1;
                break;
            }

            case Freq:
                if (!require(cursor, 3, rxLength))
                {
                    result.status = Status::TruncatedCommand;
                    return result;
                }
                jtag.setFrequencyKhz((uint16_t)((uint16_t)rx[cursor + 1] << 8) |
                                     rx[cursor + 2]);
                cursor += 3;
                break;

            case Xfer:
            {
                if (!require(cursor, 2, rxLength))
                {
                    result.status = Status::TruncatedCommand;
                    return result;
                }

                uint16_t bitCount = rx[cursor + 1];
                if (commandByte & kExtendLength) bitCount += 256;
                if (bitCount > 62u * 8u) bitCount = 62u * 8u;
                const size_t byteCount = (bitCount + 7u) / 8u;

                if (!require(cursor + 2, byteCount, rxLength))
                {
                    result.status = Status::TruncatedCommand;
                    return result;
                }

                const bool noRead = (commandByte & kNoRead) != 0;
                uint8_t* output = nullptr;
                if (!noRead)
                {
                    if (!reserve(result.responseLength, byteCount, txCapacity))
                    {
                        result.status = Status::ResponseOverflow;
                        return result;
                    }
                    output = tx + result.responseLength;
                    memset(output, 0, byteCount);
                }

                jtag.transfer(bitCount, rx + cursor + 2, output);
                if (!noRead) result.responseLength += byteCount;
                cursor += 2 + byteCount;
                break;
            }

            case SetSig:
            {
                if (!require(cursor, 3, rxLength))
                {
                    result.status = Status::TruncatedCommand;
                    return result;
                }
                const uint8_t mask = rx[cursor + 1];
                const uint8_t value = rx[cursor + 2];
                if (mask & SigTck)  jtag.setTck((value & SigTck) != 0);
                if (mask & SigTdi)  jtag.setTdi((value & SigTdi) != 0);
                if (mask & SigTms)  jtag.setTms((value & SigTms) != 0);
                if (mask & SigTrst) jtag.setTrst((value & SigTrst) != 0);
                if (mask & SigSrst) jtag.setSrst((value & SigSrst) != 0);
                cursor += 3;
                break;
            }

            case GetSig:
                if (!reserve(result.responseLength, 1, txCapacity))
                {
                    result.status = Status::ResponseOverflow;
                    return result;
                }
                tx[result.responseLength++] = jtag.getTdo() ? SigTdo : 0;
                cursor += 1;
                break;

            case Clock:
            {
                if (!require(cursor, 3, rxLength))
                {
                    result.status = Status::TruncatedCommand;
                    return result;
                }
                const uint8_t signals = rx[cursor + 1];
                const bool tdo = jtag.clock(rx[cursor + 2],
                                            (signals & SigTms) != 0,
                                            (signals & SigTdi) != 0);
                if (commandByte & kReadout)
                {
                    if (!reserve(result.responseLength, 1, txCapacity))
                    {
                        result.status = Status::ResponseOverflow;
                        return result;
                    }
                    tx[result.responseLength++] = tdo ? 0xFF : 0x00;
                }
                cursor += 3;
                break;
            }

            case Release:
                result.releaseRequested = true;
                return result;

            default:
                result.status = Status::UnsupportedCommand;
                return result;
        }
    }

    return result;
}

} // namespace DirtyJtag
