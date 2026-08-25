#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libusb.h>

enum { Vid = 0x1a86, Pid = 0x8010, EndpointOut = 0x01, EndpointIn = 0x81 };

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static const char *operation_name(uint8_t operation)
{
    switch (operation) {
    case 1: return "read";
    case 2: return "write";
    default: return "none";
    }
}

static const char *transport_name(uint8_t transport)
{
    switch (transport) {
    case 1: return "RVSWD";
    case 2: return "RVSWIO";
    default: return "none";
    }
}

static const char *status_name(uint8_t status)
{
    switch (status) {
    case 0: return "ok";
    case 1: return "engine-timeout";
    case 2: return "parity";
    case 3: return "busy-timeout";
    case 4: return "target-fault";
    default: return "unknown";
    }
}

static libusb_device_handle *open_probe(libusb_context *context, const char *wanted_serial)
{
    libusb_device **devices = NULL;
    const ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) return NULL;

    libusb_device_handle *selected = NULL;
    for (ssize_t i = 0; i < count && selected == NULL; ++i) {
        struct libusb_device_descriptor descriptor;
        if (libusb_get_device_descriptor(devices[i], &descriptor) != 0 ||
            descriptor.idVendor != Vid || descriptor.idProduct != Pid)
            continue;

        libusb_device_handle *candidate = NULL;
        if (libusb_open(devices[i], &candidate) != 0) continue;

        if (wanted_serial != NULL) {
            unsigned char serial[256] = {};
            const int length = descriptor.iSerialNumber == 0 ? -1 :
                libusb_get_string_descriptor_ascii(candidate, descriptor.iSerialNumber,
                                                   serial, sizeof(serial));
            if (length < 0 || strcmp((const char *)serial, wanted_serial) != 0) {
                libusb_close(candidate);
                continue;
            }
        }
        selected = candidate;
    }
    libusb_free_device_list(devices, 1);
    return selected;
}

int main(int argc, char **argv)
{
    const char *serial = NULL;
    int clear = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--clear") == 0)
            clear = 1;
        else if (serial == NULL)
            serial = argv[i];
        else {
            fprintf(stderr, "Usage: %s [serial] [--clear]\n", argv[0]);
            return 2;
        }
    }

    libusb_context *context = NULL;
    if (libusb_init(&context) != 0) {
        fprintf(stderr, "Unable to initialize libusb\n");
        return 1;
    }

    libusb_device_handle *probe = open_probe(context, serial);
    if (probe == NULL) {
        fprintf(stderr, "WCH-Link 1a86:8010%s%s not found or inaccessible\n",
                serial ? " serial " : "", serial ? serial : "");
        libusb_exit(context);
        return 1;
    }
    if (libusb_claim_interface(probe, 0) != 0) {
        fprintf(stderr, "Unable to claim WCH-Link interface 0\n");
        libusb_close(probe);
        libusb_exit(context);
        return 1;
    }

    uint8_t request[] = {0x81, 0x7f, 0x01, clear ? 0x01 : 0x00};
    uint8_t reply[64] = {};
    int transferred = 0;
    int result = libusb_bulk_transfer(probe, EndpointOut, request, sizeof(request),
                                      &transferred, 1000);
    if (result == 0 && transferred == (int)sizeof(request))
        result = libusb_bulk_transfer(probe, EndpointIn, reply, sizeof(reply),
                                      &transferred, 1000);

    libusb_release_interface(probe, 0);
    libusb_close(probe);
    libusb_exit(context);

    if (result != 0) {
        fprintf(stderr, "USB diagnostic command failed: %s\n", libusb_error_name(result));
        return 1;
    }
    if (clear) {
        if (transferred < 4 || reply[0] != 0x82 || reply[1] != 0x7f) {
            fprintf(stderr, "Malformed clear reply\n");
            return 1;
        }
        puts("WCH transport diagnostics cleared");
        return 0;
    }
    if (transferred != 43 || reply[0] != 0x82 || reply[1] != 0x7f ||
        reply[2] != 40 || reply[3] != 1) {
        fprintf(stderr, "Unsupported or malformed diagnostic reply (%d bytes)\n", transferred);
        return 1;
    }
    if (reply[4] == 0) {
        puts("No WCH transport has been selected yet");
        return 0;
    }

    printf("transport: %s; wire frames: %u, busy: %u, target faults: %u, parity: %u, timeouts: %u\n",
           transport_name(reply[6]), read_be32(reply + 23), read_be32(reply + 27),
           read_be32(reply + 31), read_be32(reply + 35), read_be32(reply + 39));
    if (reply[5] == 0) {
        puts("No transport error latched");
        return 0;
    }

    printf("first error: %s DMI[0x%02x], %s, data=0x%08x, raw-status=%u",
           operation_name(reply[7]), reply[8], status_name(reply[9]),
           read_be32(reply + 19), reply[10]);
    if (reply[11] != 0xff)
        printf(", parity received/expected=%u/%u", reply[11], reply[12]);
    printf("\nraw target bytes:");
    const uint8_t raw_length = reply[13] > 5 ? 5 : reply[13];
    for (uint8_t i = 0; i < raw_length; ++i) printf(" %02x", reply[14 + i]);
    putchar('\n');
    return 0;
}
