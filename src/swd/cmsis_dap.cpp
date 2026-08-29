#include "cmsis_dap.hpp"

#include <string.h>

namespace CmsisDap
{
namespace
{
constexpr uint8_t IdInfo = kCommandInfo;
constexpr uint8_t IdHostStatus = kCommandHostStatus;
constexpr uint8_t IdConnect = kCommandConnect;
constexpr uint8_t IdDisconnect = kCommandDisconnect;
constexpr uint8_t IdTransferConfigure = 0x04;
constexpr uint8_t IdTransfer = 0x05;
constexpr uint8_t IdTransferBlock = 0x06;
constexpr uint8_t IdWriteAbort = 0x08;
constexpr uint8_t IdDelay = 0x09;
constexpr uint8_t IdResetTarget = 0x0a;
constexpr uint8_t IdSwjPins = 0x10;
constexpr uint8_t IdSwjClock = 0x11;
constexpr uint8_t IdSwjSequence = 0x12;
constexpr uint8_t IdSwdConfigure = 0x13;
constexpr uint8_t IdSwdSequence = 0x1d;
constexpr uint8_t IdInvalid = 0xff;

constexpr uint8_t DapOk = 0x00;
constexpr uint8_t DapError = 0xff;
constexpr uint8_t PortDisabled = kPortDisabled;
constexpr uint8_t PortSwd = kPortSwd;

constexpr uint8_t TransferAp = 1u << 0;
constexpr uint8_t TransferRead = 1u << 1;
constexpr uint8_t TransferMatchValue = 1u << 4;
constexpr uint8_t TransferMatchMask = 1u << 5;
constexpr uint8_t DpReadBuffer = TransferRead | (3u << 2);
constexpr const char* FirmwareVersion = "0.10.0-tapioca";

uint16_t read16(const uint8_t* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t read32(const uint8_t* data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

void write16(uint8_t* data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

void write32(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

bool room(size_t offset, size_t count, size_t capacity)
{
    return offset <= capacity && count <= capacity - offset;
}

enum class TransferPacketCheck : uint8_t
{
    Ok,
    Invalid,
    ResponseOverflow,
};

TransferPacketCheck checkTransferPacket(const uint8_t* request,
                                        size_t requestLength,
                                        size_t responseCapacity)
{
    if (requestLength < 3 || responseCapacity < 3)
        return TransferPacketCheck::Invalid;

    size_t input = 3;
    size_t output = 3;
    bool overflow = false;
    for (uint8_t index = 0; index < request[2]; ++index)
    {
        if (!room(input, 1, requestLength)) return TransferPacketCheck::Invalid;
        const uint8_t transferRequest = request[input++];
        const bool read = (transferRequest & TransferRead) != 0;

        if (transferRequest & TransferMatchValue)
        {
            if (!room(input, 4, requestLength)) return TransferPacketCheck::Invalid;
            input += 4;
        }
        else if (read)
        {
            if (!room(output, 4, responseCapacity)) overflow = true;
            else output += 4;
        }
        else
        {
            if (!room(input, 4, requestLength)) return TransferPacketCheck::Invalid;
            input += 4;
        }
    }
    return overflow ? TransferPacketCheck::ResponseOverflow : TransferPacketCheck::Ok;
}

Result invalid()
{
    return {Status::InvalidPacket, 0};
}

size_t infoString(uint8_t* output, size_t capacity, const char* value)
{
    const size_t length = strlen(value) + 1;
    if (capacity < length + 2 || length > 255) return 0;
    output[0] = IdInfo;
    output[1] = (uint8_t)length;
    memcpy(output + 2, value, length);
    return length + 2;
}
}

bool commandRequiresConnection(uint8_t command)
{
    switch (command)
    {
        case IdTransfer:
        case IdTransferBlock:
        case IdWriteAbort:
        case IdResetTarget:
        case IdSwjPins:
        case IdSwjSequence:
        case IdSwdSequence:
            return true;
        default:
            return command >= 0x80u && command <= 0x9fu;
    }
}

Result disconnectedResponse(uint8_t command,
                            uint8_t* response,
                            size_t responseCapacity)
{
    if (!response || responseCapacity == 0) return invalid();

    response[0] = command;
    if (command == IdTransfer)
    {
        if (responseCapacity < 3) return invalid();
        response[1] = 0;
        response[2] = ISwd::AckError;
        return {Status::Ok, 3};
    }
    if (command == IdTransferBlock)
    {
        if (responseCapacity < 4) return invalid();
        response[1] = 0;
        response[2] = 0;
        response[3] = ISwd::AckError;
        return {Status::Ok, 4};
    }
    if (command == IdResetTarget)
    {
        if (responseCapacity < 3) return invalid();
        response[1] = DapError;
        response[2] = 0;
        return {Status::Ok, 3};
    }
    if (command >= 0x80u && command <= 0x9fu)
    {
        response[0] = IdInvalid;
        return {Status::Ok, 1};
    }
    if (responseCapacity < 2) return invalid();
    response[1] = DapError;
    return {Status::Ok, 2};
}

void Core::resetConnection(ISwd& swd)
{
    swd.disconnect();
    connected_ = false;
}

uint8_t Core::transferRetry(ISwd& swd, uint8_t request, uint32_t* data)
{
    uint16_t retries = retryCount_;
    uint8_t ack;
    do
    {
        ack = swd.transfer((uint8_t)(request & 0x0f), data);
    }
    while (ack == ISwd::AckWait && retries-- != 0);
    return ack;
}

Result Core::processTransfer(ISwd& swd,
                             const uint8_t* request,
                             size_t requestLength,
                             uint8_t* response,
                             size_t responseCapacity)
{
    const TransferPacketCheck check =
        checkTransferPacket(request, requestLength, responseCapacity);
    if (check == TransferPacketCheck::Invalid) return invalid();

    size_t input = 3;
    size_t output = 3;
    const uint8_t requestedCount = request[2];
    uint8_t completed = 0;
    uint8_t ack = ISwd::AckOk;
    bool postedRead = false;
    bool writePending = false;

    response[0] = IdTransfer;
    response[1] = 0;
    response[2] = 0;

    // Reject the whole packet before touching the target so a host retry cannot
    // duplicate writes that preceded an oversized read response.
    if (check == TransferPacketCheck::ResponseOverflow)
    {
        response[2] = ISwd::AckError;
        return {Status::Ok, 3};
    }

    for (uint8_t index = 0; index < requestedCount; ++index)
    {
        if (!room(input, 1, requestLength)) return invalid();
        const uint8_t transferRequest = request[input++];
        const bool read = (transferRequest & TransferRead) != 0;
        const bool ap = (transferRequest & TransferAp) != 0;

        // Value matching is legal CMSIS-DAP but unused by OpenOCD's normal
        // SWD path. Reject it explicitly instead of silently mis-executing it.
        if (transferRequest & TransferMatchValue)
        {
            if (!room(input, 4, requestLength)) return invalid();
            input += 4;
            ack = ISwd::AckError;
            break;
        }

        uint32_t data = 0;
        if (read)
        {
            writePending = false;

            if (postedRead)
            {
                if (ap)
                {
                    ack = transferRetry(swd, transferRequest, &data);
                    if (ack != ISwd::AckOk) break;
                    if (!room(output, 4, responseCapacity)) return invalid();
                    write32(response + output, data);
                    output += 4;
                    ++completed;
                    continue;
                }

                ack = transferRetry(swd, DpReadBuffer, &data);
                if (ack != ISwd::AckOk) break;
                if (!room(output, 4, responseCapacity)) return invalid();
                write32(response + output, data);
                output += 4;
                ++completed;
                postedRead = false;
            }

            if (ap)
            {
                ack = transferRetry(swd, transferRequest, nullptr);
                if (ack != ISwd::AckOk) break;
                postedRead = true;
            }
            else
            {
                ack = transferRetry(swd, transferRequest, &data);
                if (ack != ISwd::AckOk) break;
                if (!room(output, 4, responseCapacity)) return invalid();
                write32(response + output, data);
                output += 4;
                ++completed;
            }
        }
        else
        {
            if (!room(input, 4, requestLength)) return invalid();
            data = read32(request + input);
            input += 4;

            if (transferRequest & TransferMatchMask)
            {
                matchMask_ = data;
                ack = ISwd::AckOk;
                ++completed;
                continue;
            }

            if (postedRead)
            {
                uint32_t previous = 0;
                ack = transferRetry(swd, DpReadBuffer, &previous);
                if (ack != ISwd::AckOk) break;
                if (!room(output, 4, responseCapacity)) return invalid();
                write32(response + output, previous);
                output += 4;
                ++completed;
                postedRead = false;
            }

            ack = transferRetry(swd, transferRequest, &data);
            if (ack != ISwd::AckOk) break;
            ++completed;
            writePending = true;
        }
    }

    if (ack == ISwd::AckOk && postedRead)
    {
        uint32_t data = 0;
        ack = transferRetry(swd, DpReadBuffer, &data);
        if (ack == ISwd::AckOk)
        {
            if (!room(output, 4, responseCapacity)) return invalid();
            write32(response + output, data);
            output += 4;
            ++completed;
        }
    }
    else if (ack == ISwd::AckOk && writePending)
    {
        ack = transferRetry(swd, DpReadBuffer, nullptr);
    }

    response[1] = completed;
    response[2] = ack;
    return {Status::Ok, output};
}

Result Core::processTransferBlock(ISwd& swd,
                                  const uint8_t* request,
                                  size_t requestLength,
                                  uint8_t* response,
                                  size_t responseCapacity)
{
    if (requestLength < 5 || responseCapacity < 4) return invalid();
    uint16_t count = read16(request + 2);
    const uint8_t transferRequest = request[4];
    const bool read = (transferRequest & TransferRead) != 0;
    const bool ap = (transferRequest & TransferAp) != 0;
    size_t input = 5;
    size_t output = 4;
    uint16_t completed = 0;
    uint8_t ack = ISwd::AckOk;

    response[0] = IdTransferBlock;
    write16(response + 1, 0);
    response[3] = 0;

    if (read)
    {
        if (!room(output, (size_t)count * 4u, responseCapacity)) return invalid();
        if (ap && count)
        {
            ack = transferRetry(swd, transferRequest, nullptr);
            if (ack != ISwd::AckOk) count = 0;
        }

        while (count--)
        {
            uint32_t data = 0;
            const uint8_t current = (ap && count == 0) ? DpReadBuffer : transferRequest;
            ack = transferRetry(swd, current, &data);
            if (ack != ISwd::AckOk) break;
            write32(response + output, data);
            output += 4;
            ++completed;
        }
    }
    else
    {
        if (!room(input, (size_t)count * 4u, requestLength)) return invalid();
        while (count--)
        {
            uint32_t data = read32(request + input);
            input += 4;
            ack = transferRetry(swd, transferRequest, &data);
            if (ack != ISwd::AckOk) break;
            ++completed;
        }
        if (ack == ISwd::AckOk)
            ack = transferRetry(swd, DpReadBuffer, nullptr);
    }

    write16(response + 1, completed);
    response[3] = ack;
    return {Status::Ok, output};
}

Result Core::processPacket(ISwd& swd,
                           const uint8_t* request,
                           size_t requestLength,
                           uint8_t* response,
                           size_t responseCapacity)
{
    if (!request || !response || requestLength == 0 || responseCapacity == 0)
        return invalid();

    const uint8_t command = request[0];
    response[0] = command;

    if (!connected_ && commandRequiresConnection(command))
        return disconnectedResponse(command, response, responseCapacity);

    switch (command)
    {
        case IdInfo:
        {
            if (requestLength < 2 || responseCapacity < 2) return invalid();
            switch (request[1])
            {
                case 0x01: return {Status::Ok, infoString(response, responseCapacity, "DirtyJTAG OS")};
                case 0x02: return {Status::Ok, infoString(response, responseCapacity, "CH32X035 CMSIS-DAP")};
                case 0x03: return {Status::Ok, infoString(response, responseCapacity, "CH32X035")};
                case 0x04: return {Status::Ok, infoString(response, responseCapacity, FirmwareVersion)};
                case 0xf0:
                    if (responseCapacity < 3) return invalid();
                    response[1] = 1; response[2] = 1u << 0; // SWD only
                    return {Status::Ok, 3};
                case 0xfe:
                    if (responseCapacity < 3) return invalid();
                    response[1] = 1; response[2] = 1; // one outstanding packet
                    return {Status::Ok, 3};
                case 0xff:
                    if (responseCapacity < 4) return invalid();
                    response[1] = 2; write16(response + 2, (uint16_t)kPacketSize);
                    return {Status::Ok, 4};
                default:
                    response[1] = 0;
                    return {Status::Ok, 2};
            }
        }

        case IdHostStatus:
            if (requestLength < 3 || responseCapacity < 2) return invalid();
            response[1] = DapOk;
            return {Status::Ok, 2};

        case IdConnect:
            if (requestLength < 2 || responseCapacity < 2) return invalid();
            if (request[1] == 0 || request[1] == PortSwd)
            {
                swd.activate();
                connected_ = true;
                response[1] = PortSwd;
            }
            else response[1] = PortDisabled;
            return {Status::Ok, 2};

        case IdDisconnect:
            if (responseCapacity < 2) return invalid();
            resetConnection(swd);
            response[1] = DapOk;
            return {Status::Ok, 2};

        case IdTransferConfigure:
            if (requestLength < 6 || responseCapacity < 2) return invalid();
            swd.setIdleCycles(request[1]);
            retryCount_ = read16(request + 2);
            matchRetry_ = read16(request + 4);
            response[1] = DapOk;
            return {Status::Ok, 2};

        case IdTransfer:
            return processTransfer(swd, request, requestLength, response, responseCapacity);

        case IdTransferBlock:
            return processTransferBlock(swd, request, requestLength, response, responseCapacity);

        case IdWriteAbort:
        {
            if (requestLength < 6 || responseCapacity < 2) return invalid();
            uint32_t data = read32(request + 2);
            response[1] = transferRetry(swd, 0, &data) == ISwd::AckOk ?
                          DapOk : DapError;
            return {Status::Ok, 2};
        }

        case IdDelay:
            if (requestLength < 3 || responseCapacity < 2) return invalid();
            swd.delayUs(read16(request + 1));
            response[1] = DapOk;
            return {Status::Ok, 2};

        case IdResetTarget:
            if (responseCapacity < 3) return invalid();
            response[1] = swd.resetTarget() ? DapOk : DapError;
            response[2] = 0; // no device-specific reset sequence executed
            return {Status::Ok, 3};

        case IdSwjPins:
            if (requestLength < 7 || responseCapacity < 2) return invalid();
            if (!swd.writePins(request[1], request[2])) resetConnection(swd);
            response[1] = swd.readPins();
            return {Status::Ok, 2};

        case IdSwjClock:
            if (requestLength < 5 || responseCapacity < 2) return invalid();
            if (read32(request + 1) == 0) response[1] = DapError;
            else { swd.setClockHz(read32(request + 1)); response[1] = DapOk; }
            return {Status::Ok, 2};

        case IdSwjSequence:
        {
            if (requestLength < 2 || responseCapacity < 2) return invalid();
            const uint16_t bits = request[1] == 0 ? 256 : request[1];
            const size_t bytes = (bits + 7u) / 8u;
            if (!room(2, bytes, requestLength)) return invalid();
            response[1] = swd.writeSequence(bits, request + 2) ? DapOk : DapError;
            return {Status::Ok, 2};
        }

        case IdSwdConfigure:
            if (requestLength < 2 || responseCapacity < 2) return invalid();
            swd.setTurnaround((uint8_t)((request[1] & 0x03u) + 1u));
            swd.setDataPhase((request[1] & 0x04u) != 0);
            response[1] = DapOk;
            return {Status::Ok, 2};

        case IdSwdSequence:
        {
            if (requestLength < 2 || responseCapacity < 2) return invalid();
            size_t input = 2;
            size_t output = 2;
            response[1] = DapOk;
            for (uint8_t sequence = 0; sequence < request[1]; ++sequence)
            {
                if (!room(input, 1, requestLength)) return invalid();
                const uint8_t info = request[input++];
                const uint16_t bits = (info & 0x3fu) == 0 ? 64 : (info & 0x3fu);
                const size_t bytes = (bits + 7u) / 8u;
                if (info & 0x80u)
                {
                    if (!room(output, bytes, responseCapacity)) return invalid();
                    if (!swd.readSequence(bits, response + output))
                    {
                        response[1] = DapError;
                        return {Status::Ok, output};
                    }
                    output += bytes;
                }
                else
                {
                    if (!room(input, bytes, requestLength)) return invalid();
                    if (!swd.writeSequence(bits, request + input))
                    {
                        response[1] = DapError;
                        return {Status::Ok, output};
                    }
                    input += bytes;
                }
            }
            return {Status::Ok, output};
        }

        default:
            if (command >= 0x80u && command <= 0x9fu)
            {
                const size_t length = swd.vendorCommand(request, requestLength,
                                                        response, responseCapacity);
                if (length != 0) return {Status::Ok, length};
            }
            response[0] = IdInvalid;
            return {Status::Ok, 1};
    }
}

} // namespace CmsisDap
