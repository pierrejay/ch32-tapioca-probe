// WCH-Link command decoder tests using byte-level minichlink fixtures.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wchlink/protocol.hpp"
#include "wch_link_fixtures.hpp"

namespace F = WchLinkFixtures;
using WchLink::DmiStatus;

// Configurable fake DMI transport (same role as FakeJtag in protocol_test.cpp).
class FakeDmiPort final : public WchLink::IDmi
{
public:
    bool connect() override { ++connectCalls; return connectResult; }

    DmiStatus readDmi(uint8_t address, uint32_t& value) override
    {
        ++readCalls;
        lastReadAddr = address;
        value = readValue;
        return readStatus;
    }

    DmiStatus writeDmi(uint8_t address, uint32_t value) override
    {
        ++writeCalls;
        lastWriteAddr = address;
        lastWriteValue = value;
        return writeStatus;
    }

    void disconnect() override { ++disconnectCalls; }

    // configuration
    bool connectResult = true;
    uint32_t readValue = 0x00300500; // CH32V003F4P6 identity register
    DmiStatus readStatus = DmiStatus::Ok;
    DmiStatus writeStatus = DmiStatus::Ok;

    // observations
    int connectCalls = 0, readCalls = 0, writeCalls = 0, disconnectCalls = 0;
    uint8_t lastReadAddr = 0xff, lastWriteAddr = 0xff;
    uint32_t lastWriteValue = 0;
};

int main()
{
    uint8_t tx[64];

    // ---- invalid buffers: no out-of-bounds write -------------------------------
    {
        FakeDmiPort port;
        WchLink::Core core;
        uint8_t guarded[] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5};

        auto r = core.processPacket(port, F::kIdentifyReq, sizeof(F::kIdentifyReq),
                                    guarded + 1, 3);
        assert(r.responseLength == 0);
        for (uint8_t byte : guarded) assert(byte == 0xa5);

        r = core.processPacket(port, F::kIdentifyReq, sizeof(F::kIdentifyReq),
                               nullptr, 64);
        assert(r.responseLength == 0);

        r = core.processPacket(port, nullptr, 0, tx, sizeof(tx));
        assert(r.responseLength == 4 && tx[0] == 0x82);

        r = core.processPacket(port, F::kIdentifyReq, sizeof(F::kIdentifyReq), tx, 4);
        assert(r.responseLength == 4 && tx[0] == 0x82);
        assert(port.connectCalls == 0 && port.readCalls == 0 && port.writeCalls == 0);
    }

    // ---- identify: exact bytes + session reset ---------------------------------
    {
        FakeDmiPort port;
        WchLink::Core core;
        memset(tx, 0xaa, sizeof(tx));
        const auto r = core.processPacket(port, F::kIdentifyReq, sizeof(F::kIdentifyReq), tx, sizeof(tx));
        assert(r.status == WchLink::Status::Ok);
        assert(r.responseLength == sizeof(F::kIdentifyReply));
        assert(memcmp(tx, F::kIdentifyReply, r.responseLength) == 0);
        assert(!core.connected());
    }

    // ---- connect: framed chip identity, session becomes connected --------------
    {
        FakeDmiPort port;
        WchLink::Core core;
        const auto r = core.processPacket(port, F::kConnectReq, sizeof(F::kConnectReq), tx, sizeof(tx));
        assert(r.responseLength == sizeof(F::kConnectReply));
        assert(port.connectCalls == 1);
        assert(port.readCalls == 1 && port.lastReadAddr == 0x7f);
        assert(core.connected());
        assert(memcmp(tx, F::kConnectReply, r.responseLength) == 0);
    }

    // ---- target identity selects the family and is returned big-endian ---------
    {
        struct IdentityCase { uint32_t chipId; uint8_t family; };
        constexpr IdentityCase cases[] = {
            {0x00300500, 0x09}, // CH32V003F4P6
            {0x035e0601, 0x0d}, // CH32X035F8U6
            {0x20370500, 0x05}, // CH32V203F6P6
            {0x30700508, 0x06}, // CH32V307VCT6
        };

        for (const IdentityCase& identity : cases)
        {
            FakeDmiPort port;
            port.readValue = identity.chipId;
            WchLink::Core core;
            const auto r = core.processPacket(port, F::kConnectReq, sizeof(F::kConnectReq), tx, sizeof(tx));
            assert(r.status == WchLink::Status::Ok);
            assert(r.responseLength == 8);
            assert(tx[3] == identity.family);
            assert(tx[4] == (uint8_t)(identity.chipId >> 24));
            assert(tx[5] == (uint8_t)(identity.chipId >> 16));
            assert(tx[6] == (uint8_t)(identity.chipId >> 8));
            assert(tx[7] == (uint8_t)identity.chipId);
            assert(core.connected());
        }
    }

    // ---- connect with no target present: explicit error, session not held ------
    {
        FakeDmiPort port;
        port.connectResult = false;
        WchLink::Core core;
        const auto r = core.processPacket(port, F::kConnectReq, sizeof(F::kConnectReq), tx, sizeof(tx));
        assert(r.status == WchLink::Status::TargetUnavailable);
        assert(r.responseLength == sizeof(F::kConnectErrorReply));
        assert(memcmp(tx, F::kConnectErrorReply, r.responseLength) == 0);
        assert(port.readCalls == 0);
        assert(!core.connected());
    }

    // ---- unreadable or unsupported identity also releases the session ----------
    {
        FakeDmiPort port;
        port.readStatus = DmiStatus::Timeout;
        WchLink::Core core;
        auto r = core.processPacket(port, F::kConnectReq, sizeof(F::kConnectReq), tx, sizeof(tx));
        assert(r.status == WchLink::Status::TargetUnavailable);
        assert(port.disconnectCalls == 1 && !core.connected());

        port = FakeDmiPort{};
        port.readValue = 0xdeadbeef;
        WchLink::Core otherCore;
        r = otherCore.processPacket(port, F::kConnectReq, sizeof(F::kConnectReq), tx, sizeof(tx));
        assert(r.status == WchLink::Status::TargetUnavailable);
        assert(port.disconnectCalls == 1 && !otherCore.connected());
    }

    // ---- DMI read: big-endian data from the fake, exact fixture reply ----------
    {
        FakeDmiPort port;
        port.readValue = F::dmiReplyData(F::kDmiReadReply); // 0x00030382
        WchLink::Core core;
        const auto r = core.processPacket(port, F::kDmiReadReq, sizeof(F::kDmiReadReq), tx, sizeof(tx));
        assert(r.status == WchLink::Status::Ok);
        assert(r.responseLength == 9);
        assert(port.readCalls == 1 && port.lastReadAddr == 0x11);
        assert(port.writeCalls == 0);
        assert(memcmp(tx, F::kDmiReadReply, 9) == 0);
    }

    // ---- DMI write: address + big-endian data reach the wire, data echoed ------
    {
        FakeDmiPort port;
        WchLink::Core core;
        const auto r = core.processPacket(port, F::kDmiWriteReq, sizeof(F::kDmiWriteReq), tx, sizeof(tx));
        assert(r.responseLength == 9);
        assert(port.writeCalls == 1);
        assert(port.lastWriteAddr == 0x10);
        assert(port.lastWriteValue == 0x80000001u);
        assert(port.readCalls == 0);
        assert(memcmp(tx, F::kDmiWriteReply, 9) == 0);
    }

    // ---- DMI error: a transport failure surfaces as host-visible status --------
    {
        FakeDmiPort port;
        port.readStatus = DmiStatus::Timeout;
        WchLink::Core core;
        const auto r = core.processPacket(port, F::kDmiReadReq, sizeof(F::kDmiReadReq), tx, sizeof(tx));
        assert(r.responseLength == 9);
        assert(F::dmiReplyIsError(tx, (uint8_t)r.responseLength));
        assert(tx[8] == 0x02);
    }

    // ---- truncated DMI op: bounded error reply, wire untouched -----------------
    {
        FakeDmiPort port;
        WchLink::Core core;
        const auto r = core.processPacket(port, F::kDmiReadReq, 5, tx, sizeof(tx)); // 4 bytes short
        assert(r.status == WchLink::Status::TruncatedCommand);
        assert(r.responseLength >= 1);
        assert(port.readCalls == 0 && port.writeCalls == 0); // never touched the wire
    }

    // ---- stop: releases the session --------------------------------------------
    {
        FakeDmiPort port;
        WchLink::Core core;
        core.processPacket(port, F::kConnectReq, sizeof(F::kConnectReq), tx, sizeof(tx));
        assert(core.connected());
        const auto r = core.processPacket(port, F::kStopReq, sizeof(F::kStopReq), tx, sizeof(tx));
        assert(r.responseLength >= 1);
        assert(!core.connected());
        assert(port.disconnectCalls >= 1);
    }

    // ---- unknown command: safe non-empty ack, never a wire transaction ---------
    {
        FakeDmiPort port;
        WchLink::Core core;
        const uint8_t unknown[] = {0x81, 0x99, 0x00};
        const auto r = core.processPacket(port, unknown, sizeof(unknown), tx, sizeof(tx));
        assert(r.status == WchLink::Status::UnsupportedCommand);
        assert(r.responseLength >= 1 && tx[0] == 0x82);
        assert(port.readCalls == 0 && port.writeCalls == 0);
    }

    // ---- TRANSPORT INVARIANT: every fixture command yields a non-empty reply ---
    // This is the "never stall EP1" guarantee across the whole command set.
    {
        for (size_t i = 0; i < F::kCount; ++i)
        {
            FakeDmiPort port;
            WchLink::Core core;
            const F::Fixture& f = F::kAll[i];
            const auto r = core.processPacket(port, f.request, f.requestLen, tx, sizeof(tx));
            assert(r.responseLength >= 1);
            assert(tx[0] == 0x82);
        }
    }

    printf("wch_link_protocol_test: all cases OK\n");
    return 0;
}
