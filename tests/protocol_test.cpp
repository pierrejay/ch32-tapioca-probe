#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "dirtyjtag/protocol.hpp"

class FakeJtag final : public IJtag
{
public:
    void setFrequencyKhz(uint16_t value) override { frequency = value; }
    void setTck(bool value) override { tck = value; }
    void setTdi(bool value) override { tdi = value; }
    void setTms(bool value) override { tms = value; }
    bool getTdo() const override { return tdo; }
    void setTrst(bool value) override { trst = value; }
    void setSrst(bool value) override { srst = value; }

    void transfer(uint16_t bits, const uint8_t* in, uint8_t* out) override
    {
        transferBits = bits;
        transferCalls++;
        if (out) memcpy(out, in, (bits + 7u) / 8u);
    }

    bool clock(uint8_t count, bool tmsValue, bool tdiValue) override
    {
        pulses = count;
        tms = tmsValue;
        tdi = tdiValue;
        return tdo;
    }

    uint16_t frequency = 0;
    uint16_t transferBits = 0;
    uint8_t pulses = 0;
    unsigned transferCalls = 0;
    bool tck = false;
    bool tdi = false;
    bool tdo = true;
    bool tms = false;
    bool trst = true;
    bool srst = true;
};

int main()
{
    FakeJtag jtag;
    uint8_t tx[64] = {};

    {
        const uint8_t rx[] = {0x01, 0x00};
        const auto result = DirtyJtag::processPacket(jtag, rx, sizeof(rx), tx);
        assert(result.status == DirtyJtag::Status::Ok);
        assert(result.responseLength == 10);
        assert(memcmp(tx, "DJTAG2\n", 7) == 0);
    }

    {
        const uint8_t rx[] = {0x02, 0x01, 0xF4, 0x00};
        const auto result = DirtyJtag::processPacket(jtag, rx, sizeof(rx), tx);
        assert(result.status == DirtyJtag::Status::Ok);
        assert(jtag.frequency == 500);
    }

    {
        const uint8_t rx[] = {0x03, 9, 0xA5, 0x80, 0x00};
        memset(tx, 0, sizeof(tx));
        const auto result = DirtyJtag::processPacket(jtag, rx, sizeof(rx), tx);
        assert(result.status == DirtyJtag::Status::Ok);
        assert(result.responseLength == 2);
        assert(jtag.transferBits == 9);
        assert(tx[0] == 0xA5 && tx[1] == 0x80);
    }

    {
        const uint8_t rx[] = {0x83, 8, 0x5A, 0x05, 0x00};
        const unsigned before = jtag.transferCalls;
        const auto result = DirtyJtag::processPacket(jtag, rx, sizeof(rx), tx);
        assert(result.status == DirtyJtag::Status::Ok);
        assert(result.responseLength == 1); // GETSIG only; XFER is NO_READ
        assert(jtag.transferCalls == before + 1);
        assert(tx[0] == (1u << 3));
    }

    {
        const uint8_t rx[] = {0x09, 0x00};
        const auto result = DirtyJtag::processPacket(jtag, rx, sizeof(rx), tx);
        assert(result.status == DirtyJtag::Status::UnsupportedCommand);
    }

    {
        const uint8_t rx[] = {0x04, (1u << 4) | (1u << 6), (1u << 4), 0x00};
        const auto result = DirtyJtag::processPacket(jtag, rx, sizeof(rx), tx);
        assert(result.status == DirtyJtag::Status::Ok);
        assert(jtag.tms);
        assert(!jtag.srst);
    }

    {
        const uint8_t rx[] = {0x86, (1u << 4) | (1u << 2), 5, 0x00};
        const auto result = DirtyJtag::processPacket(jtag, rx, sizeof(rx), tx);
        assert(result.status == DirtyJtag::Status::Ok);
        assert(result.responseLength == 1 && tx[0] == 0xFF);
        assert(jtag.pulses == 5 && jtag.tms && jtag.tdi);
    }

    {
        const uint8_t rx[] = {0x03, 16, 0xAA};
        const auto result = DirtyJtag::processPacket(jtag, rx, sizeof(rx), tx);
        assert(result.status == DirtyJtag::Status::TruncatedCommand);
    }

    {
        const unsigned before = jtag.transferCalls;
        const uint8_t rx[] = {0x07, 0x03, 8, 0xAA, 0x00};
        const auto result = DirtyJtag::processPacket(jtag, rx, sizeof(rx), tx);
        assert(result.status == DirtyJtag::Status::Ok);
        assert(result.releaseRequested);
        assert(jtag.transferCalls == before); // release terminates the packet
    }

    return 0;
}
