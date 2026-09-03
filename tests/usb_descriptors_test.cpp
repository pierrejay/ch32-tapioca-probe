#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "usb/cmsis_dap_descriptors.hpp"
#include "usb/wchlink_descriptors.hpp"

namespace
{
constexpr uint8_t kConfiguration = 0x02;
constexpr uint8_t kInterface = 0x04;
constexpr uint8_t kEndpoint = 0x05;
constexpr uint8_t kIad = 0x0b;

template <size_t N>
void checkBaseConfiguration(const uint8_t (&descriptor)[N], uint8_t interfaceCount,
                            uint8_t outEndpoint, uint8_t inEndpoint)
{
    assert(N >= 9);
    assert(descriptor[0] == 9 && descriptor[1] == kConfiguration);
    assert((descriptor[2] | (static_cast<uint16_t>(descriptor[3]) << 8)) == N);
    assert(descriptor[4] == interfaceCount);

    std::array<bool, 16> interfaces{};
    std::array<bool, 256> endpoints{};
    for (size_t offset = 0; offset < N;)
    {
        const uint8_t length = descriptor[offset];
        assert(length >= 2 && offset + length <= N);
        if (descriptor[offset + 1] == kInterface)
        {
            assert(length >= 9 && descriptor[offset + 2] < interfaces.size());
            interfaces[descriptor[offset + 2]] = true;
        }
        if (descriptor[offset + 1] == kEndpoint)
        {
            const uint8_t address = descriptor[offset + 2];
            assert(!endpoints[address]);
            endpoints[address] = true;
            assert(address != 0x85 && address != 0x06 && address != 0x87);
        }
        offset += length;
    }
    for (uint8_t i = 0; i < interfaceCount; ++i) assert(interfaces[i]);
    assert(endpoints[outEndpoint] && endpoints[inEndpoint]);
}

template <size_t N>
void checkUartConfiguration(const uint8_t (&descriptor)[N], uint8_t interfaceCount,
                            uint8_t cdcControl, uint8_t cdcData,
                            uint8_t notifyEndpoint, uint8_t outEndpoint,
                            uint8_t inEndpoint)
{
    assert(N >= 9);
    assert(descriptor[0] == 9 && descriptor[1] == kConfiguration);
    assert((descriptor[2] | (static_cast<uint16_t>(descriptor[3]) << 8)) == N);
    assert(descriptor[4] == interfaceCount);

    std::array<bool, 16> interfaces{};
    std::array<bool, 256> endpoints{};
    bool foundCdcIad = false;

    for (size_t offset = 0; offset < N;)
    {
        const uint8_t length = descriptor[offset];
        assert(length >= 2 && offset + length <= N);
        const uint8_t type = descriptor[offset + 1];

        if (type == kInterface)
        {
            assert(length >= 9 && descriptor[offset + 2] < interfaces.size());
            interfaces[descriptor[offset + 2]] = true;
        }
        else if (type == kEndpoint)
        {
            assert(length >= 7);
            const uint8_t address = descriptor[offset + 2];
            assert(!endpoints[address]);
            endpoints[address] = true;
        }
        else if (type == kIad && length >= 8 &&
                 descriptor[offset + 2] == cdcControl)
        {
            assert(descriptor[offset + 3] == 2);
            assert(descriptor[offset + 4] == 0x02);
            foundCdcIad = true;
        }
        offset += length;
    }

    for (uint8_t i = 0; i < interfaceCount; ++i) assert(interfaces[i]);
    assert(foundCdcIad && interfaces[cdcControl] && interfaces[cdcData]);
    assert(endpoints[static_cast<uint8_t>(0x80 | notifyEndpoint)]);
    assert(endpoints[outEndpoint]);
    assert(endpoints[static_cast<uint8_t>(0x80 | inEndpoint)]);
}
}

int main()
{
    namespace Dap = CmsisDapUsbDescriptors;
    namespace Wch = WchLinkUsbDescriptors;

    checkBaseConfiguration(Dap::configuration, 1, 0x01, 0x82);
    checkBaseConfiguration(Wch::configuration, 1, 0x01, 0x81);
    assert(Dap::configuration[8] == Dap::kMaxPower);
    assert(Wch::configuration[8] == Wch::kMaxPower);
    checkUartConfiguration(Dap::configurationWithUart, 3,
                           Dap::kCdcControlInterface, Dap::kCdcDataInterface,
                           Dap::kCdcNotifyEndpoint, Dap::kCdcOutEndpoint,
                           Dap::kCdcInEndpoint);
    checkUartConfiguration(Wch::configurationWithUart, 3,
                           Wch::kCdcControlInterface, Wch::kCdcDataInterface,
                           Wch::kCdcNotifyEndpoint, Wch::kCdcOutEndpoint,
                           Wch::kCdcInEndpoint);
    assert(Dap::configurationWithUart[8] == Dap::kMaxPower);
    assert(Wch::configurationWithUart[8] == Wch::kMaxPower);
    assert(Wch::device[4] == 0x00);
    assert(Wch::deviceWithUart[4] == 0xef);
    assert(Dap::device[4] == 0x00);
    assert(Dap::deviceWithUart[4] == 0xef);

    std::cout << "usb_descriptors_test: base and UART descriptors OK\n";
}
