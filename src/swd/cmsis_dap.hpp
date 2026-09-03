#pragma once

#include <stddef.h>
#include <stdint.h>

#include "jtag/jtag_interface.hpp"
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
constexpr uint8_t kPortJtag = 2;

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
    // A null JTAG backend keeps the existing SWD-only personality and
    // capability advertisement.
    Result processPacket(ISwd& swd,
                         IJtag* jtag,
                         const uint8_t* request,
                         size_t requestLength,
                         uint8_t* response,
                         size_t responseCapacity = kPacketSize);
    Result processPacket(ISwd& swd,
                         const uint8_t* request,
                         size_t requestLength,
                         uint8_t* response,
                         size_t responseCapacity = kPacketSize)
    {
        return processPacket(swd, nullptr, request, requestLength,
                             response, responseCapacity);
    }

    bool connected() const { return activePort_ != kPortDisabled; }
    uint8_t activePort() const { return activePort_; }
    void resetConnection(ISwd& swd);

private:
    static constexpr uint8_t kMaxJtagDevices = 8;

    uint8_t transferRetry(ISwd& swd, uint8_t request, uint32_t* data);
    uint8_t jtagTransferRetry(IJtag& jtag, uint8_t index,
                              uint8_t request, uint32_t* data);
    Result processSwdTransfer(ISwd& swd, const uint8_t* request,
                              size_t requestLength, uint8_t* response,
                              size_t responseCapacity);
    Result processSwdTransferBlock(ISwd& swd, const uint8_t* request,
                                   size_t requestLength, uint8_t* response,
                                   size_t responseCapacity);
    Result processJtagTransfer(IJtag& jtag, const uint8_t* request,
                               size_t requestLength, uint8_t* response,
                               size_t responseCapacity);
    Result processJtagTransferBlock(IJtag& jtag, const uint8_t* request,
                                    size_t requestLength, uint8_t* response,
                                    size_t responseCapacity);
    bool configureJtag(const uint8_t* lengths, uint8_t count);
    void selectJtagIr(IJtag& jtag, uint8_t index, uint32_t instruction);
    uint8_t transferJtagWord(IJtag& jtag, uint8_t index,
                             uint8_t request, uint32_t* data);
    uint32_t readJtagIdCode(IJtag& jtag, uint8_t index);
    void writeJtagAbort(IJtag& jtag, uint8_t index, uint32_t data);

    uint16_t retryCount_ = 100;
    IJtag* activeJtag_ = nullptr;
    uint8_t activePort_ = kPortDisabled;
    uint8_t idleCycles_ = 0;
    uint8_t jtagDeviceCount_ = 0;
    uint8_t jtagIrLength_[kMaxJtagDevices] = {};
    uint16_t jtagIrBefore_[kMaxJtagDevices] = {};
    uint16_t jtagIrAfter_[kMaxJtagDevices] = {};
};

} // namespace CmsisDap
