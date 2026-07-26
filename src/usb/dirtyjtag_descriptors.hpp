#pragma once

#include <stdint.h>

namespace DirtyJtagUsbDescriptors
{

constexpr uint16_t kVid = 0x1209;
constexpr uint16_t kPid = 0xC0CA;
constexpr uint8_t kEp0Size = 64;
constexpr uint16_t kPacketSize = 64;

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
    0xEF, 0x02, 0x01,       // Miscellaneous / Common Class / IAD -> multi-function composite
    kEp0Size,
    (uint8_t)kVid, (uint8_t)(kVid >> 8),
    (uint8_t)kPid, (uint8_t)(kPid >> 8),
    0x00, 0x02,             // DirtyJTAG firmware 2.00
    Manufacturer, Product, Serial,
    0x01,
};

constexpr uint8_t configuration[] =
{
    // Composite configuration: two independent functions, each wrapped in an
    // Interface Association Descriptor so macOS/Windows instantiate both interfaces
    // on their own - without a libusb SET_CONFIGURATION priming the device first.
    0x09, 0x02, 0x47, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,   // wTotalLength = 71
    // IAD: function 0 = DirtyJTAG (interface 0, vendor class).
    0x08, 0x0B, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00,
    // Interface 0: DirtyJTAG v2.
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,
    // EP1 OUT: host -> probe.
    0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x01,
    // EP2 IN: probe -> host.
    0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x01,
    // IAD: function 1 = CMSIS-DAP (interface 1, vendor class).
    0x08, 0x0B, 0x01, 0x01, 0xFF, 0x00, 0x00, CmsisDapInterface,
    // Interface 1: CMSIS-DAP v2. Endpoint order is mandated by CMSIS-DAP:
    // bulk OUT first, then bulk IN.
    0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x00, 0x00, CmsisDapInterface,
    0x07, 0x05, 0x04, 0x02, 0x40, 0x00, 0x01,
    0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x01,
};

constexpr uint8_t lang[] = {0x04, 0x03, 0x09, 0x04};

constexpr uint8_t manufacturer[] =
{
    0x1A, 0x03,
    'D',0,'i',0,'r',0,'t',0,'y',0,'J',0,'T',0,'A',0,'G',0,
    ' ',0,'O',0,'S',0
};

constexpr uint8_t product[] =
{
    0x3A, 0x03,
    'C',0,'H',0,'3',0,'2',0,'X',0,'0',0,'3',0,'5',0,' ',0,
    'D',0,'i',0,'r',0,'t',0,'y',0,'J',0,'T',0,'A',0,'G',0,' ',0,
    'C',0,'M',0,'S',0,'I',0,'S',0,'-',0,'D',0,'A',0,'P',0
};

constexpr uint8_t cmsisDapInterface[] =
{
    0x1A, 0x03,
    'C',0,'M',0,'S',0,'I',0,'S',0,'-',0,'D',0,'A',0,'P',0,
    ' ',0,'v',0,'2',0
};

// The Serial string (index 3) is built at runtime from the chip UID; see
// usb_serial.hpp and the Desc::Serial case in usb_dirtyjtag.cpp.

static_assert(sizeof(device) == 18, "invalid USB device descriptor length");
static_assert(sizeof(configuration) == 71, "invalid USB configuration length");
static_assert(sizeof(manufacturer) == manufacturer[0], "invalid manufacturer string length");
static_assert(sizeof(product) == product[0], "invalid product string length");
static_assert(sizeof(cmsisDapInterface) == cmsisDapInterface[0], "invalid CMSIS-DAP interface string length");

inline uint16_t configurationLength()
{
    return (uint16_t)configuration[2] | ((uint16_t)configuration[3] << 8);
}

} // namespace DirtyJtagUsbDescriptors
