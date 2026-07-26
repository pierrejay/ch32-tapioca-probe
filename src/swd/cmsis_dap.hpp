#pragma once

#include <stddef.h>
#include <stdint.h>

#include "swd/swd_interface.hpp"

namespace CmsisDap
{

constexpr size_t kPacketSize = 64;
constexpr uint8_t kCommandInfo = 0x00;
constexpr uint8_t kCommandHostStatus = 0x01;
constexpr uint8_t kCommandConnect = 0x02;
constexpr uint8_t kCommandDisconnect = 0x03;
constexpr uint8_t kPortDisabled = 0;
constexpr uint8_t kPortSwd = 1;

enum class Status : uint8_t
{
    Ok,
    InvalidPacket,
};

struct Result
{
    Status status;
    size_t responseLength;
};

// Commands which may drive or sample target pins. They are rejected until a
// successful DAP_Connect, so enumeration/configuration cannot accidentally
// wake the SWD engine while another transport owns the probe.
bool commandRequiresConnection(uint8_t command);
Result disconnectedResponse(uint8_t command,
                            uint8_t* response,
                            size_t responseCapacity = kPacketSize);

class Core
{
public:
    Result processPacket(ISwd& swd,
                         const uint8_t* request,
                         size_t requestLength,
                         uint8_t* response,
                         size_t responseCapacity = kPacketSize);

    bool connected() const { return connected_; }
    void resetConnection(ISwd& swd);

private:
    uint8_t transferRetry(ISwd& swd, uint8_t request, uint32_t* data);
    Result processTransfer(ISwd& swd, const uint8_t* request, size_t requestLength,
                           uint8_t* response, size_t responseCapacity);
    Result processTransferBlock(ISwd& swd, const uint8_t* request, size_t requestLength,
                                uint8_t* response, size_t responseCapacity);

    uint16_t retryCount_ = 100;
    uint16_t matchRetry_ = 0;
    uint32_t matchMask_ = 0;
    bool connected_ = false;
};

} // namespace CmsisDap
