#pragma once

#include <stddef.h>
#include <stdint.h>

#include "dmi_interface.hpp"

// WCH-Link (minichlink -C linke) USB command decoder. Protocol documentation and
// byte-level test fixtures:
//   docs/wch-link-usb-protocol.md
//   tests/wch_link_fixtures.hpp
//
// Transport-independent: it consumes one EP1 OUT command packet and produces the
// EP1 IN reply. It never runs a wire transaction itself - it calls IDmi.
namespace WchLink
{

constexpr size_t kPacketSize = 64;

// Programmer type reported to minichlink. 0x12 (18) is decoded as WCH-LinkE
// (pgm-wch-linke.c:402).
constexpr uint8_t kProgrammerTypeLinkE = 0x12;

enum class Status : uint8_t
{
    Ok,                 // recognized command, handled.
    TruncatedCommand,   // command too short for its family; error reply emitted.
    UnsupportedCommand, // unrecognized command; safe ack emitted.
    TargetUnavailable,  // target absent, unreadable, or not recognized.
};

struct Result
{
    size_t responseLength = 0;
    Status status = Status::Ok;
};

// Session-owning command decoder.
//
// With a valid output buffer of at least four bytes, processPacket always produces
// a non-empty reply. The USB caller supplies a full 64-byte packet buffer.
class Core
{
public:
    Result processPacket(IDmi& port,
                         const uint8_t* rx,
                         size_t rxLength,
                         uint8_t* tx,
                         size_t txCapacity = kPacketSize);

    bool connected() const { return connected_; }

    // Release any session: USB reset/suspend, or a fresh personality claim.
    void reset(IDmi& port);

private:
    bool connected_ = false;
};

} // namespace WchLink
