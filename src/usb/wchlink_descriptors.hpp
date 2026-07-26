#pragma once

#include <stdint.h>

// USB descriptors for the WCH-Link Tapioca personality. minichlink -C linke
// matches on VID:PID 1a86:8010, claims interface 0, and uses bulk OUT 0x01 /
// bulk IN 0x81 (docs/wch-link-usb-protocol.md). It does not inspect the strings.
//
// Compatibility identity only: 1a86:8010 is WCH's VID:PID, borrowed so stock host
// tools detect the probe with no config (see the USB-identity disclaimer in the
// README). The strings are honest (manufacturer "Tapioca") since the host ignores
// them; only the numeric VID:PID is borrowed. This never touches the DirtyJTAG
// identity 1209:C0CA.
namespace WchLinkUsbDescriptors
{

constexpr uint16_t kVid = 0x1a86;
constexpr uint16_t kPid = 0x8010;
constexpr uint8_t kEp0Size = 64;
constexpr uint16_t kPacketSize = 64;

enum StringId : uint8_t
{
    Lang = 0,
    Manufacturer = 1,
    Product = 2,
    Serial = 3,
};

constexpr uint8_t device[] =
{
    0x12, 0x01,             // bLength, DEVICE
    0x10, 0x01,             // USB 1.10
    0xFF, 0x00, 0x00,       // vendor class; minichlink drives the bulk interface
    kEp0Size,
    (uint8_t)kVid, (uint8_t)(kVid >> 8),
    (uint8_t)kPid, (uint8_t)(kPid >> 8),
    0x00, 0x01,             // bcdDevice 1.00
    Manufacturer, Product, Serial,
    0x01,                   // one configuration
};

constexpr uint8_t configuration[] =
{
    // Single vendor interface: one bulk OUT + one bulk IN endpoint.
    0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    // Interface 0: vendor-specific, 2 endpoints.
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,
    // EP1 OUT: host -> probe, bulk, 64.
    0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,
    // EP1 IN: probe -> host, bulk, 64.
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
};

constexpr uint8_t lang[] = {0x04, 0x03, 0x09, 0x04};

constexpr uint8_t manufacturer[] =
{
    0x10, 0x03,
    'T',0,'a',0,'p',0,'i',0,'o',0,'c',0,'a',0
};

constexpr uint8_t product[] =
{
    0x22, 0x03,
    'W',0,'C',0,'H',0,'-',0,'L',0,'i',0,'n',0,'k',0,' ',0,
    'T',0,'a',0,'p',0,'i',0,'o',0,'c',0,'a',0
};

// The Serial string (index 3) is built at runtime from the chip UID; see
// usb_serial.hpp and the Desc::Serial case in usb_wchlink.cpp.

static_assert(sizeof(device) == 18, "invalid USB device descriptor length");
static_assert(sizeof(configuration) == 32, "invalid USB configuration length");
static_assert(sizeof(manufacturer) == manufacturer[0], "invalid manufacturer string length");
static_assert(sizeof(product) == product[0], "invalid product string length");

inline uint16_t configurationLength()
{
    return (uint16_t)configuration[2] | ((uint16_t)configuration[3] << 8);
}

} // namespace WchLinkUsbDescriptors
