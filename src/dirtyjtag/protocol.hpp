#pragma once

#include <stddef.h>
#include <stdint.h>

#include "jtag_interface.hpp"

namespace DirtyJtag
{

constexpr size_t kPacketSize = 64;

enum class Status : uint8_t
{
    Ok,
    TruncatedCommand,
    ResponseOverflow,
    UnsupportedCommand,
};

struct Result
{
    size_t responseLength = 0;
    Status status = Status::Ok;
    bool releaseRequested = false;
};

// Process exactly one USB bulk OUT packet. Multiple DirtyJTAG commands may be
// batched in the packet. The returned response, if any, is written to tx.
Result processPacket(IJtag& jtag,
                     const uint8_t* rx,
                     size_t rxLength,
                     uint8_t* tx,
                     size_t txCapacity = kPacketSize);

} // namespace DirtyJtag
