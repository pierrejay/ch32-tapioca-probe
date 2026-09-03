#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "swd/cmsis_dap.hpp"

class FakeSwd final : public ISwd
{
public:
    void init() override {}
    void activate() override { active = true; }
    void disconnect() override { active = false; }
    void setClockHz(uint32_t value) override { clockHz = value; }
    void setTurnaround(uint8_t value) override { turnaround = value; }
    void setDataPhase(bool value) override { dataPhase = value; }
    void setIdleCycles(uint8_t value) override { idle = value; }

    uint8_t transfer(uint8_t request, uint32_t* data) override
    {
        const uint8_t result = transferCount < ackSequenceLength
            ? ackSequence[transferCount] : ack;
        lastRequest = request;
        if (transferCount < sizeof(requests)) requests[transferCount] = request;
        ++transferCount;
        if (data && (request & 0x02u)) *data = nextRead++;
        else if (data) lastWrite = *data;
        return result;
    }

    bool writeSequence(uint16_t bits, const uint8_t* data) override
    {
        sequenceBits = bits;
        sequenceFirstByte = data[0];
        return sequenceOk;
    }

    bool readSequence(uint16_t bits, uint8_t* data) override
    {
        sequenceBits = bits;
        memset(data, 0xa5, (bits + 7u) / 8u);
        return sequenceOk;
    }

    bool writePins(uint8_t value, uint8_t select) override
    {
        pins = (uint8_t)((pins & ~select) | (value & select));
        return pinsOk;
    }
    uint8_t readPins() const override { return pins; }
    bool resetTarget() override { ++resetCount; return true; }
    void delayUs(uint32_t value) override { delay = value; }
    size_t vendorCommand(const uint8_t* request, size_t requestLength,
                         uint8_t* response, size_t responseCapacity) override
    {
        ++vendorCount;
        if (requestLength < 2 || responseCapacity < 3 || request[0] != 0x80) return 0;
        response[0] = request[0];
        response[1] = 0;
        response[2] = request[1];
        return 3;
    }

    uint32_t clockHz = 0;
    uint32_t nextRead = 0x11223344;
    uint32_t lastWrite = 0;
    uint16_t sequenceBits = 0;
    uint8_t sequenceFirstByte = 0;
    uint8_t lastRequest = 0;
    uint8_t transferCount = 0;
    uint8_t requests[32] = {};
    uint8_t ackSequence[32] = {};
    uint8_t ackSequenceLength = 0;
    uint8_t turnaround = 0;
    uint8_t idle = 0;
    uint8_t pins = 0x80;
    uint8_t ack = AckOk;
    uint32_t delay = 0;
    unsigned resetCount = 0;
    unsigned vendorCount = 0;
    bool dataPhase = false;
    bool active = false;
    bool sequenceOk = true;
    bool pinsOk = true;
};

class FakeJtag final : public IJtag
{
public:
    void init() override {}
    void activate() override { active = true; }
    void disconnect() override { active = false; }
    void setClockHz(uint32_t value) override { clockHz = value; }
    void setTck(bool high) override { setPin(0x01, high); }
    void setTms(bool high) override { setPin(0x02, high); }
    void setTdi(bool high) override { setPin(0x04, high); }
    bool getTdo() const override { return (pins & 0x08u) != 0; }
    void setTrst(bool high) override { setPin(0x20, high); }
    void setSrst(bool high) override { setPin(0x80, high); }

    void sequence(uint16_t bits, bool tms, const uint8_t* input, uint8_t* output) override
    {
        ++sequenceCount;
        lastSequenceBits = bits;
        lastSequenceTms = tms;
        for (uint16_t bit = 0; bit < bits; ++bit)
            record(tms, input &&
                        (input[bit >> 3u] & (1u << (bit & 7u))) != 0);
        if (!output) return;

        memset(output, 0, (bits + 7u) / 8u);
        if (bits == 3)
        {
            output[0] = static_cast<uint8_t>(((ack & 0x02u) >> 1u) |
                                             ((ack & 0x01u) << 1u) |
                                              (ack & 0x04u));
        }
        else if (bits == 31)
        {
            const uint32_t value = nextRead++;
            output[0] = static_cast<uint8_t>(value);
            output[1] = static_cast<uint8_t>(value >> 8u);
            output[2] = static_cast<uint8_t>(value >> 16u);
            output[3] = static_cast<uint8_t>(value >> 24u) & 0x7fu;
            pendingLastTdo = (value & 0x80000000u) != 0;
            pendingLastValid = true;
        }
        else
        {
            memset(output, captureByte, (bits + 7u) / 8u);
        }
    }

    bool clock(uint8_t pulses, bool tms, bool tdi) override
    {
        if (pulses == 0) ++zeroPulseClockCalls;
        clockCycles += pulses;
        setTms(tms);
        setTdi(tdi);
        for (uint8_t pulse = 0; pulse < pulses; ++pulse) record(tms, tdi);
        if (pendingLastValid)
        {
            pendingLastValid = false;
            return pendingLastTdo;
        }
        return getTdo();
    }

    bool writePins(uint8_t value, uint8_t select) override
    {
        pins = static_cast<uint8_t>((pins & ~select) | (value & select));
        return pinsOk;
    }
    uint8_t readPins() const override { return pins; }
    bool resetTarget() override { ++resetCount; return true; }
    void delayUs(uint32_t value) override { delay = value; }
    void startTrace() { traceCount = 0; traceEnabled = true; }
    void stopTrace() { traceEnabled = false; }

    uint32_t clockHz = 0;
    uint32_t clockCycles = 0;
    uint32_t zeroPulseClockCalls = 0;
    uint32_t nextRead = 0x12345670u;
    uint32_t delay = 0;
    uint16_t lastSequenceBits = 0;
    unsigned sequenceCount = 0;
    unsigned resetCount = 0;
    uint16_t traceCount = 0;
    bool traceTms[256] = {};
    bool traceTdi[256] = {};
    uint8_t pins = 0xa0;
    uint8_t ack = ISwd::AckOk;
    uint8_t captureByte = 0x5a;
    bool active = false;
    bool pinsOk = true;
    bool lastSequenceTms = false;
    bool traceEnabled = false;

private:
    void record(bool tms, bool tdi)
    {
        if (!traceEnabled) return;
        assert(traceCount < 256);
        traceTms[traceCount] = tms;
        traceTdi[traceCount] = tdi;
        ++traceCount;
    }

    void setPin(uint8_t mask, bool high)
    {
        pins = static_cast<uint8_t>((pins & ~mask) | (high ? mask : 0));
    }

    bool pendingLastTdo = false;
    bool pendingLastValid = false;
};

int main()
{
    FakeSwd swd;
    CmsisDap::Core dap;
    uint8_t tx[64] = {};

    {
        const uint8_t rx[] = {0x00, 0x02};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.status == CmsisDap::Status::Ok);
        assert(tx[0] == 0x00 && tx[1] != 0);
        assert(strstr((const char*)tx + 2, "CMSIS-DAP") != nullptr);
    }

    {
        FakeSwd jtagSwd;
        FakeJtag jtag;
        CmsisDap::Core jtagDap;

        const uint8_t capabilities[] = {0x00, 0xf0};
        auto result = jtagDap.processPacket(jtagSwd, &jtag, capabilities,
                                            sizeof(capabilities), tx);
        assert(result.responseLength == 3 && tx[2] == 0x03);

        const uint8_t configure[] = {0x15, 1, 4};
        result = jtagDap.processPacket(jtagSwd, &jtag, configure,
                                       sizeof(configure), tx);
        assert(result.responseLength == 2 && tx[1] == 0);

        // Reject an invalid replacement atomically: the original one-device
        // chain must remain usable.
        const uint8_t invalidConfigure[] = {0x15, 2, 4, 0};
        result = jtagDap.processPacket(jtagSwd, &jtag, invalidConfigure,
                                       sizeof(invalidConfigure), tx);
        assert(result.responseLength == 2 && tx[1] == 0xff);

        const uint8_t connect[] = {0x02, CmsisDap::kPortJtag};
        result = jtagDap.processPacket(jtagSwd, &jtag, connect,
                                       sizeof(connect), tx);
        assert(result.responseLength == 2 && tx[1] == CmsisDap::kPortJtag);
        assert(jtag.active && !jtagSwd.active);
        assert(jtagDap.activePort() == CmsisDap::kPortJtag);

        const uint8_t sequence[] = {0x14, 1, 0x88, 0xa5};
        result = jtagDap.processPacket(jtagSwd, &jtag, sequence,
                                       sizeof(sequence), tx);
        assert(result.responseLength == 3 && tx[1] == 0 && tx[2] == 0x5a);
        assert(jtag.lastSequenceBits == 8 && !jtag.lastSequenceTms);

        jtag.nextRead = 0x8ba01477u;
        jtag.startTrace();
        const uint8_t idCode[] = {0x16, 0};
        result = jtagDap.processPacket(jtagSwd, &jtag, idCode,
                                       sizeof(idCode), tx);
        assert(result.responseLength == 6 && tx[1] == 0);
        assert(tx[2] == 0x77 && tx[3] == 0x14 &&
               tx[4] == 0xa0 && tx[5] == 0x8b);
        // Golden one-TAP path: select IDCODE through Shift-IR, then read 32
        // bits through Shift-DR and return to Run-Test/Idle.
        assert(jtag.traceCount == 47);
        for (uint16_t bit = 0; bit < jtag.traceCount; ++bit)
        {
            const bool expectedTms = bit == 0 || bit == 1 || bit == 7 ||
                                     bit == 8 || bit == 10 || bit == 44 ||
                                     bit == 45;
            assert(jtag.traceTms[bit] == expectedTms);
        }
        jtag.stopTrace();

        jtag.nextRead = 0x10203040u;
        const uint8_t transfer[] = {0x05, 0, 1, 0x02};
        result = jtagDap.processPacket(jtagSwd, &jtag, transfer,
                                       sizeof(transfer), tx);
        assert(result.responseLength == 7);
        assert(tx[1] == 1 && tx[2] == ISwd::AckOk);
        assert(tx[3] == 0x41 && tx[4] == 0x30 &&
               tx[5] == 0x20 && tx[6] == 0x10);

        const uint8_t block[] = {0x06, 0, 2, 0, 0x02};
        result = jtagDap.processPacket(jtagSwd, &jtag, block,
                                       sizeof(block), tx);
        assert(result.responseLength == 12);
        assert(tx[1] == 2 && tx[2] == 0 && tx[3] == ISwd::AckOk);

        const unsigned sequencesBeforeOversizedBlock = jtag.sequenceCount;
        const uint8_t oversizedBlock[] = {0x06, 0, 16, 0, 0x02};
        result = jtagDap.processPacket(jtagSwd, &jtag, oversizedBlock,
                                       sizeof(oversizedBlock), tx);
        assert(result.status == CmsisDap::Status::Ok);
        assert(result.responseLength == 4 && tx[1] == 0 && tx[2] == 0 &&
               tx[3] == ISwd::AckError);
        assert(jtag.sequenceCount == sequencesBeforeOversizedBlock);

        const uint8_t truncatedWriteBlock[] = {
            0x06, 0, 2, 0, 0x00, 0x44, 0x33, 0x22, 0x11,
        };
        result = jtagDap.processPacket(jtagSwd, &jtag, truncatedWriteBlock,
                                       sizeof(truncatedWriteBlock), tx);
        assert(result.status == CmsisDap::Status::Ok);
        assert(result.responseLength == 4 && tx[1] == 0 && tx[2] == 0 &&
               tx[3] == ISwd::AckError);
        assert(jtag.sequenceCount == sequencesBeforeOversizedBlock);

        const uint32_t cyclesBeforeEmptyBlock = jtag.clockCycles;
        const unsigned sequencesBeforeEmptyBlock = jtag.sequenceCount;
        const uint8_t emptyBlock[] = {0x06, 0, 0, 0, 0x00};
        result = jtagDap.processPacket(jtagSwd, &jtag, emptyBlock,
                                       sizeof(emptyBlock), tx);
        assert(result.responseLength == 4 && tx[1] == 0 && tx[2] == 0 &&
               tx[3] == ISwd::AckOk);
        assert(jtag.clockCycles == cyclesBeforeEmptyBlock);
        assert(jtag.sequenceCount == sequencesBeforeEmptyBlock);

        const uint8_t clock[] = {0x11, 0x40, 0x42, 0x0f, 0x00};
        result = jtagDap.processPacket(jtagSwd, &jtag, clock,
                                       sizeof(clock), tx);
        assert(result.responseLength == 2 && tx[1] == 0);
        assert(jtag.clockHz == 1000000u);

        const uint8_t swdSequence[] = {0x1d, 1, 8, 0x5a};
        result = jtagDap.processPacket(jtagSwd, &jtag, swdSequence,
                                       sizeof(swdSequence), tx);
        assert(result.responseLength == 2 && tx[1] == 0xff);
        assert(jtagSwd.sequenceBits == 0);

        const uint8_t connectSwd[] = {0x02, CmsisDap::kPortSwd};
        result = jtagDap.processPacket(jtagSwd, &jtag, connectSwd,
                                       sizeof(connectSwd), tx);
        assert(result.responseLength == 2 && tx[1] == CmsisDap::kPortSwd);
        assert(!jtag.active && jtagSwd.active);

        const uint8_t disconnect[] = {0x03};
        // The core tracks the active backend for the connection lifetime, so a
        // session reset cannot strand JTAG pins by omitting the optional input.
        result = jtagDap.processPacket(jtagSwd, disconnect,
                                       sizeof(disconnect), tx);
        assert(result.responseLength == 2 && tx[1] == 0);
        assert(!jtag.active && !jtagSwd.active && !jtagDap.connected());
    }

    {
        // Match the STM32H523 chain used by OpenOCD: the four-bit ARM DP is
        // followed by a five-bit boundary-scan TAP. Exercise bypass bits on
        // both sides instead of covering only the trivial one-TAP case.
        FakeSwd jtagSwd;
        FakeJtag jtag;
        CmsisDap::Core jtagDap;

        const uint8_t configure[] = {0x15, 2, 4, 5};
        auto result = jtagDap.processPacket(jtagSwd, &jtag, configure,
                                            sizeof(configure), tx);
        assert(result.responseLength == 2 && tx[1] == 0);

        const uint8_t connect[] = {0x02, CmsisDap::kPortJtag};
        result = jtagDap.processPacket(jtagSwd, &jtag, connect,
                                       sizeof(connect), tx);
        assert(result.responseLength == 2 && tx[1] == CmsisDap::kPortJtag);

        jtag.nextRead = 0x06478041u;
        const uint8_t secondIdCode[] = {0x16, 1};
        result = jtagDap.processPacket(jtagSwd, &jtag, secondIdCode,
                                       sizeof(secondIdCode), tx);
        assert(result.responseLength == 6 && tx[1] == 0);
        assert(tx[2] == 0x41 && tx[3] == 0x80 &&
               tx[4] == 0x47 && tx[5] == 0x06);

        jtag.nextRead = 0x10203040u;
        jtag.zeroPulseClockCalls = 0;
        jtag.startTrace();
        const uint8_t transfer[] = {0x05, 0, 1, 0x02};
        result = jtagDap.processPacket(jtagSwd, &jtag, transfer,
                                       sizeof(transfer), tx);
        jtag.stopTrace();
        assert(result.responseLength == 7);
        assert(tx[1] == 1 && tx[2] == ISwd::AckOk);
        assert(tx[3] == 0x41 && tx[4] == 0x30 &&
               tx[5] == 0x20 && tx[6] == 0x10);

        // 15 clocks select DPACC while shifting BYPASS through the second TAP,
        // followed by two 41-clock posted-read transactions.
        assert(jtag.traceCount == 97);
        for (uint16_t bit = 0; bit < jtag.traceCount; ++bit)
        {
            const bool expectedTms = bit == 0 || bit == 1 ||
                                     bit == 12 || bit == 13 ||
                                     bit == 15 || bit == 53 || bit == 54 ||
                                     bit == 56 || bit == 94 || bit == 95;
            assert(jtag.traceTms[bit] == expectedTms);
        }
        // Zero-length segments still apply their requested TMS/TDI levels.
        assert(jtag.zeroPulseClockCalls == 7);

        const uint8_t sequence12[] = {0x14, 1, 0x8c, 0xa5, 0x05};
        jtag.startTrace();
        result = jtagDap.processPacket(jtagSwd, &jtag, sequence12,
                                       sizeof(sequence12), tx);
        jtag.stopTrace();
        assert(result.responseLength == 4 && jtag.traceCount == 12);
        for (uint16_t bit = 0; bit < 12; ++bit)
        {
            const bool expectedTdi =
                (sequence12[3 + (bit >> 3u)] & (1u << (bit & 7u))) != 0;
            assert(jtag.traceTdi[bit] == expectedTdi);
        }

        const uint8_t sequence33[] = {
            0x14, 1, 0xe1, 0x96, 0x69, 0xa5, 0x5a, 0x01,
        };
        jtag.startTrace();
        result = jtagDap.processPacket(jtagSwd, &jtag, sequence33,
                                       sizeof(sequence33), tx);
        jtag.stopTrace();
        assert(result.responseLength == 7 && jtag.traceCount == 33);
        for (uint16_t bit = 0; bit < 33; ++bit)
        {
            const bool expectedTdi =
                (sequence33[3 + (bit >> 3u)] & (1u << (bit & 7u))) != 0;
            assert(jtag.traceTdi[bit] == expectedTdi);
            assert(jtag.traceTms[bit]);
        }
    }

    {
        // Wire commands before DAP_Connect must fail without activating or
        // calling the physical backend.
        const uint8_t transfer[] = {0x05, 0, 1, 0x02};
        const auto transferResult = dap.processPacket(swd, transfer, sizeof(transfer), tx);
        assert(transferResult.responseLength == 3);
        assert(tx[1] == 0 && tx[2] == ISwd::AckError);
        assert(swd.transferCount == 0 && !swd.active);

        const uint8_t sequence[] = {0x12, 8, 0x9e};
        const auto sequenceResult = dap.processPacket(swd, sequence, sizeof(sequence), tx);
        assert(sequenceResult.responseLength == 2 && tx[1] == 0xff);
        assert(swd.sequenceBits == 0 && !swd.active);

        const uint8_t vendor[] = {0x80, 0x5a};
        const auto vendorResult = dap.processPacket(swd, vendor, sizeof(vendor), tx);
        assert(vendorResult.responseLength == 1 && tx[0] == 0xff);
        assert(swd.vendorCount == 0 && !swd.active);
    }

    {
        const uint8_t rx[] = {0x02, 0x00};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 2 && tx[1] == 1);
        assert(swd.active && dap.connected());
    }

    {
        const uint8_t rx[] = {0x04, 3, 7, 0, 0, 0};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 2 && tx[1] == 0);
        assert(swd.idle == 3);
    }

    {
        const uint8_t rx[] = {0x11, 0x40, 0x42, 0x0f, 0x00}; // 1 MHz
        dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(swd.clockHz == 1000000);
    }

    {
        const uint8_t rx[] = {0x12, 8, 0x9e};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 2 && tx[1] == 0);
        assert(swd.sequenceBits == 8 && swd.sequenceFirstByte == 0x9e);
    }

    {
        const uint8_t rx[] = {0x09, 0x34, 0x12};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 2 && tx[1] == 0);
        assert(swd.delay == 0x1234);
    }

    {
        const uint8_t rx[] = {0x08, 0, 0, 0, 0, 0};
        swd.ack = ISwd::AckFault;
        auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 2 && tx[1] == 0xff);
        swd.ack = ISwd::AckOk;
        result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 2 && tx[1] == 0);
    }

    {
        const uint8_t rx[] = {0x05, 0, 1, 0x02}; // one DP read
        memset(tx, 0, sizeof(tx));
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 7);
        assert(tx[0] == 0x05 && tx[1] == 1 && tx[2] == ISwd::AckOk);
        assert(tx[3] == 0x44 && tx[4] == 0x33 && tx[5] == 0x22 && tx[6] == 0x11);
    }

    {
        // Fifteen reads fit in a 64-byte response; sixteen are rejected before
        // any target access occurs.
        uint8_t rx[3 + 16] = {0x05, 0, 15};
        memset(rx + 3, 0x02, 16);
        swd.transferCount = 0;
        auto result = dap.processPacket(swd, rx, sizeof(rx) - 1, tx);
        assert(result.responseLength == 63);
        assert(tx[1] == 15 && tx[2] == ISwd::AckOk);
        assert(swd.transferCount == 15);

        rx[2] = 16;
        swd.transferCount = 0;
        result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.status == CmsisDap::Status::Ok);
        assert(result.responseLength == 3);
        assert(tx[1] == 0 && tx[2] == ISwd::AckError);
        assert(swd.transferCount == 0);
    }

    {
        // WAIT is retried up to DAP_TransferConfigure.retry_count.
        const uint8_t rx[] = {0x05, 0, 1, 0x02};
        swd.transferCount = 0;
        swd.ackSequence[0] = ISwd::AckWait;
        swd.ackSequence[1] = ISwd::AckWait;
        swd.ackSequence[2] = ISwd::AckOk;
        swd.ackSequenceLength = 3;
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.status == CmsisDap::Status::Ok);
        assert(tx[1] == 1 && tx[2] == ISwd::AckOk);
        assert(swd.transferCount == 3);
        swd.ackSequenceLength = 0;
    }

    {
        // FAULT, no target response, and protocol errors stop the packet
        // without being retried.
        const uint8_t rx[] = {0x05, 0, 1, 0x02};
        swd.transferCount = 0;
        swd.ack = ISwd::AckFault;
        dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(tx[1] == 0 && tx[2] == ISwd::AckFault);
        assert(swd.transferCount == 1);

        swd.transferCount = 0;
        swd.ack = ISwd::AckNoAck;
        dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(tx[1] == 0 && tx[2] == ISwd::AckNoAck);
        assert(swd.transferCount == 1);

        swd.transferCount = 0;
        swd.ack = ISwd::AckError;
        dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(tx[1] == 0 && tx[2] == ISwd::AckError);
        assert(swd.transferCount == 1);
        swd.ack = ISwd::AckOk;
    }

    {
        // DP SELECT write followed by an AP read must not gain an intermediate
        // RDBUFF. The AP read itself is posted and is completed by RDBUFF.
        const uint8_t rx[] = {
            0x05, 0, 2,
            0x08, 0xf0, 0x00, 0x00, 0x01,
            0x07,
        };
        swd.transferCount = 0;
        memset(swd.requests, 0, sizeof(swd.requests));
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.status == CmsisDap::Status::Ok);
        assert(tx[1] == 2 && tx[2] == ISwd::AckOk);
        assert(swd.transferCount == 3);
        assert(swd.requests[0] == 0x08);
        assert(swd.requests[1] == 0x07);
        assert(swd.requests[2] == 0x0e);
    }

    {
        // Preserve the CMSIS-DAP/ADIv5 request stream without target-specific
        // transfers inserted by the physical backend.
        const uint8_t rx[] = {
            0x05, 0, 4,
            0x08, 0xf0, 0x00, 0x00, 0x01,
            0x01, 0x52, 0x00, 0x80, 0x02,
            0x05, 0x00, 0xed, 0x00, 0xe0,
            0x0f,
        };
        swd.transferCount = 0;
        memset(swd.requests, 0, sizeof(swd.requests));
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.status == CmsisDap::Status::Ok);
        assert(tx[1] == 4 && tx[2] == ISwd::AckOk);
        assert(swd.transferCount == 5);
        assert(swd.requests[0] == 0x08);
        assert(swd.requests[1] == 0x01);
        assert(swd.requests[2] == 0x05);
        assert(swd.requests[3] == 0x0f);
        assert(swd.requests[4] == 0x0e);
    }

    {
        swd.nextRead = 0xaabbcc00;
        const uint8_t rx[] = {0x06, 0, 2, 0, 0x02}; // two DP reads
        memset(tx, 0, sizeof(tx));
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 12);
        assert(tx[0] == 0x06 && tx[1] == 2 && tx[2] == 0 && tx[3] == ISwd::AckOk);
        assert(tx[4] == 0x00 && tx[8] == 0x01);

        swd.transferCount = 0;
        swd.ack = ISwd::AckNoAck;
        const auto noAckResult = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(noAckResult.responseLength == 4);
        assert(tx[1] == 0 && tx[2] == 0 && tx[3] == ISwd::AckNoAck);
        assert(swd.transferCount == 1);
        swd.ack = ISwd::AckOk;
    }

    {
        const unsigned transfersBefore = swd.transferCount;
        const uint8_t oversizedRead[] = {0x06, 0, 16, 0, 0x02};
        auto result = dap.processPacket(swd, oversizedRead,
                                        sizeof(oversizedRead), tx);
        assert(result.status == CmsisDap::Status::Ok);
        assert(result.responseLength == 4 && tx[1] == 0 && tx[2] == 0 &&
               tx[3] == ISwd::AckError);
        assert(swd.transferCount == transfersBefore);

        const uint8_t truncatedWrite[] = {
            0x06, 0, 2, 0, 0x00, 0x44, 0x33, 0x22, 0x11,
        };
        result = dap.processPacket(swd, truncatedWrite,
                                   sizeof(truncatedWrite), tx);
        assert(result.status == CmsisDap::Status::Ok);
        assert(result.responseLength == 4 && tx[1] == 0 && tx[2] == 0 &&
               tx[3] == ISwd::AckError);
        assert(swd.transferCount == transfersBefore);
    }

    {
        const uint8_t rx[] = {0x13, 0x05}; // turnaround=2, data phase
        dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(swd.turnaround == 2 && swd.dataPhase);
    }

    {
        const uint8_t rx[] = {0x80, 0x5a};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 3);
        assert(tx[0] == 0x80 && tx[1] == 0 && tx[2] == 0x5a);
    }

    {
        const uint8_t rx[] = {0x9f};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 1 && tx[0] == 0xff);
    }

    {
        // Sequence backend failures must be visible in the CMSIS-DAP status.
        swd.sequenceOk = false;
        const uint8_t swj[] = {0x12, 8, 0x9e};
        auto result = dap.processPacket(swd, swj, sizeof(swj), tx);
        assert(result.responseLength == 2 && tx[1] == 0xff);

        const uint8_t swdWrite[] = {0x1d, 1, 8, 0x5a};
        result = dap.processPacket(swd, swdWrite, sizeof(swdWrite), tx);
        assert(result.responseLength == 2 && tx[1] == 0xff);

        const uint8_t swdRead[] = {0x1d, 1, 0x88};
        result = dap.processPacket(swd, swdRead, sizeof(swdRead), tx);
        assert(result.responseLength == 2 && tx[1] == 0xff);
        swd.sequenceOk = true;
    }

    {
        // DAP_SWJ_Pins has no status byte. If its backend fails, invalidate the
        // connection so later target commands cannot continue optimistically.
        swd.pinsOk = false;
        const uint8_t rx[] = {0x10, 0x03, 0x03, 0, 0, 0, 0};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 2);
        assert(!dap.connected() && !swd.active);
        swd.pinsOk = true;

        const uint8_t connect[] = {0x02, 0x00};
        dap.processPacket(swd, connect, sizeof(connect), tx);
        assert(dap.connected() && swd.active);
    }

    {
        const uint8_t rx[] = {0x05, 0, 1};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.status == CmsisDap::Status::InvalidPacket);
    }

    {
        const uint8_t rx[] = {0x03};
        const auto result = dap.processPacket(swd, rx, sizeof(rx), tx);
        assert(result.responseLength == 2 && tx[1] == 0);
        assert(!swd.active && !dap.connected());

        const uint8_t transfer[] = {0x05, 0, 1, 0x02};
        const unsigned before = swd.transferCount;
        dap.processPacket(swd, transfer, sizeof(transfer), tx);
        assert(swd.transferCount == before);
    }

    return 0;
}
