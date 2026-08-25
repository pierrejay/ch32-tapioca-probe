// WCH-Link command decoder tests using byte-level minichlink fixtures.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wchlink/protocol.hpp"
#include "wch_link_fixtures.hpp"

namespace F = WchLinkFixtures;
using WchLink::DmiStatus;
using WchLink::DmiOperation;
using WchLink::DmiTransport;

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

    bool getDiagnostics(WchLink::DmiDiagnostics& out) const override
    {
        out = diagnostics;
        return diagnosticsSupported;
    }

    void clearDiagnostics() override
    {
        ++clearDiagnosticsCalls;
        diagnostics = {};
    }

    void disconnect() override { ++disconnectCalls; }

    // configuration
    bool connectResult = true;
    uint32_t readValue = 0x00300500; // CH32V003F4P6 identity register
    DmiStatus readStatus = DmiStatus::Ok;
    DmiStatus writeStatus = DmiStatus::Ok;
    bool diagnosticsSupported = true;
    WchLink::DmiDiagnostics diagnostics;

    // observations
    int connectCalls = 0, readCalls = 0, writeCalls = 0, disconnectCalls = 0;
    int clearDiagnosticsCalls = 0;
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
            {0x00200600, 0x4e}, // CH32V002F4P6
            {0x00300500, 0x09}, // CH32V003F4P6
            {0x00400600, 0x4e}, // CH32V004F6P1
            {0x00500600, 0x4e}, // CH32V005E6R6
            {0x00600600, 0x4e}, // CH32V006K8U6
            {0x00710600, 0x4e}, // CH32V007E8R6
            {0x035a0601, 0x0d}, // CH32X033F8P6
            {0x035e0601, 0x0d}, // CH32X035F8U6
            {0x10310700, 0x0e}, // CH32L103C8T6
            {0x20370500, 0x05}, // CH32V203F6P6
            {0x20500500, 0xce}, // CH32V205
            {0x2080050c, 0x05}, // CH32V208WBU6
            {0x25004102, 0x01}, // CH32V103C8T6
            {0x30330504, 0x06}, // CH32V303CBT6
            {0x30520508, 0x06}, // CH32V305FBP6
            {0x30700508, 0x06}, // CH32V307VCT6
            {0x3170b508, 0x86}, // CH32V317VCT6
            {0x4150050d, 0xc6}, // CH32H415REU6
            {0x4160050d, 0xc6}, // CH32H416RDU6
            {0x4170051d, 0xc6}, // CH32H417QEU6
            {0x64100500, 0x49}, // CH641F
            {0x64300601, 0x0c}, // CH643W
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

    // ---- private diagnostics: snapshot and clear without touching the wire -----
    {
        FakeDmiPort port;
        port.diagnostics.valid = true;
        port.diagnostics.transport = DmiTransport::Rvswd;
        port.diagnostics.operation = DmiOperation::Read;
        port.diagnostics.address = 0x04;
        port.diagnostics.status = DmiStatus::Parity;
        port.diagnostics.rawStatus = 0x01;
        port.diagnostics.receivedParity = 1;
        port.diagnostics.expectedParity = 0;
        port.diagnostics.rawLength = 5;
        const uint8_t raw[] = {0x12, 0x34, 0x56, 0x78, 0x90};
        memcpy(port.diagnostics.raw, raw, sizeof(raw));
        port.diagnostics.data = 0x12345678;
        port.diagnostics.wireFrames = 0x01020304;
        port.diagnostics.busyReplies = 5;
        port.diagnostics.targetFaults = 6;
        port.diagnostics.parityErrors = 7;
        port.diagnostics.engineTimeouts = 8;

        WchLink::Core core;
        const uint8_t query[] = {0x81, 0x7f, 0x01, 0x00};
        auto r = core.processPacket(port, query, sizeof(query), tx, sizeof(tx));
        assert(r.status == WchLink::Status::Ok && r.responseLength == 43);
        assert(tx[0] == 0x82 && tx[1] == 0x7f && tx[2] == 40);
        assert(tx[3] == 1 && tx[4] == 1 && tx[5] == 1);
        assert(tx[6] == (uint8_t)DmiTransport::Rvswd);
        assert(tx[7] == (uint8_t)DmiOperation::Read && tx[8] == 0x04);
        assert(tx[9] == (uint8_t)DmiStatus::Parity && tx[10] == 1);
        assert(tx[11] == 1 && tx[12] == 0 && tx[13] == 5);
        assert(memcmp(tx + 14, raw, sizeof(raw)) == 0);
        assert(tx[19] == 0x12 && tx[20] == 0x34 && tx[21] == 0x56 && tx[22] == 0x78);
        assert(tx[23] == 0x01 && tx[24] == 0x02 && tx[25] == 0x03 && tx[26] == 0x04);
        assert(port.readCalls == 0 && port.writeCalls == 0 && port.connectCalls == 0);

        const uint8_t clear[] = {0x81, 0x7f, 0x01, 0x01};
        r = core.processPacket(port, clear, sizeof(clear), tx, sizeof(tx));
        assert(r.responseLength == 4 && port.clearDiagnosticsCalls == 1);
        assert(!port.diagnostics.valid);
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
