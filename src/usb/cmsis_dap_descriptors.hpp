#pragma once

#include <stdint.h>

namespace CmsisDapUsbDescriptors
{

// Temporary compatibility identity from Arm's CMSIS-DAP v2 example. It is not
// allocated to this project and must be replaced by the project's own VID/PID.
constexpr uint16_t kVid = 0xc251;
constexpr uint16_t kPid = 0xf000;
constexpr uint8_t kEp0Size = 64;
constexpr uint16_t kPacketSize = 64;
constexpr uint8_t kMaxPower = 250; // 500 mA in USB's 2 mA units
constexpr uint8_t kCdcControlInterface = 1;
constexpr uint8_t kCdcDataInterface = 2;
constexpr uint8_t kCdcNotifyEndpoint = 5;
constexpr uint8_t kCdcOutEndpoint = 6;
constexpr uint8_t kCdcInEndpoint = 7;

enum StringId : uint8_t
{
    Lang = 0,
    Manufacturer = 1,
    Product = 2,
    Serial = 3,
    CmsisDapInterface = 4,
};

constexpr uint8_t device[] =
{
    0x12, 0x01,             // length, device descriptor
    0x10, 0x01,             // USB 1.10
    0x00, 0x00, 0x00,       // class defined by interface
    kEp0Size,
    (uint8_t)kVid, (uint8_t)(kVid >> 8),
    (uint8_t)kPid, (uint8_t)(kPid >> 8),
    0x00, 0x02,             // firmware 2.00
    Manufacturer, Product, Serial,
    0x01,
};

constexpr uint8_t deviceWithUart[] =
{
    0x12, 0x01,
    0x10, 0x01,
    0xEF, 0x02, 0x01,       // Miscellaneous / Common Class / IAD composite
    kEp0Size,
    (uint8_t)kVid, (uint8_t)(kVid >> 8),
    (uint8_t)kPid, (uint8_t)(kPid >> 8),
    0x00, 0x02,
    Manufacturer, Product, Serial,
    0x01,
};

constexpr uint8_t configuration[] =
{
    0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, kMaxPower,
    // CMSIS-DAP v2 on interface 0. Endpoint order is mandated by CMSIS-DAP:
    // bulk OUT first, then bulk IN.
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, CmsisDapInterface,
    0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x01,
    0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x01,
};

constexpr uint8_t configurationWithUart[] =
{
    0x09, 0x02, 0x62, 0x00, 0x03, 0x01, 0x00, 0x80, kMaxPower,
    // CMSIS-DAP v2 function on interface 0.
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, CmsisDapInterface,
    0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x01,
    0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x01,

    // CDC ACM UART bridge (interfaces 1 and 2).
    0x08, 0x0B, kCdcControlInterface, 0x02, 0x02, 0x02, 0x01, 0x00,
    0x09, 0x04, kCdcControlInterface, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, kCdcDataInterface,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, kCdcControlInterface, kCdcDataInterface,
    0x07, 0x05, (uint8_t)(0x80 | kCdcNotifyEndpoint), 0x03, 0x08, 0x00, 0x10,
    0x09, 0x04, kCdcDataInterface, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    0x07, 0x05, kCdcOutEndpoint, 0x02, 0x40, 0x00, 0x00,
    0x07, 0x05, (uint8_t)(0x80 | kCdcInEndpoint), 0x02, 0x40, 0x00, 0x00,
};

constexpr uint8_t lang[] = {0x04, 0x03, 0x09, 0x04};

constexpr uint8_t manufacturer[] =
{
    0x10, 0x03,
    'T',0,'a',0,'p',0,'i',0,'o',0,'c',0,'a',0
};

constexpr uint8_t product[] =
{
    0x24, 0x03,
    'T',0,'a',0,'p',0,'i',0,'o',0,'c',0,'a',0,' ',0,
    'C',0,'M',0,'S',0,'I',0,'S',0,'-',0,'D',0,'A',0,'P',0
};

constexpr uint8_t cmsisDapInterface[] =
{
    0x1A, 0x03,
    'C',0,'M',0,'S',0,'I',0,'S',0,'-',0,'D',0,'A',0,'P',0,
    ' ',0,'v',0,'2',0
};

// The Serial string (index 3) is built at runtime from the chip UID.
static_assert(sizeof(device) == 18, "invalid USB device descriptor length");
static_assert(sizeof(deviceWithUart) == 18, "invalid UART USB device descriptor length");
static_assert(sizeof(configuration) == 32, "invalid USB configuration length");
static_assert(sizeof(configurationWithUart) == 98, "invalid UART USB configuration length");
static_assert(sizeof(manufacturer) == manufacturer[0], "invalid manufacturer string length");
static_assert(sizeof(product) == product[0], "invalid product string length");
static_assert(sizeof(cmsisDapInterface) == cmsisDapInterface[0], "invalid CMSIS-DAP interface string length");

inline uint16_t configurationLength(const uint8_t* descriptor)
{
    return (uint16_t)descriptor[2] | ((uint16_t)descriptor[3] << 8);
}

} // namespace CmsisDapUsbDescriptors
