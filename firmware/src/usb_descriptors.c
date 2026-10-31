// USB descriptors: one vendor-defined HID interface with three feature reports.
#include <string.h>

#include "hid_proto.h"
#include "pico/unique_id.h"
#include "tusb.h"

// Feature report of n bytes: report ID, vendor usage, 8-bit fields.
#define FEATURE_REPORT(id, usage, n)                                                        \
    HID_REPORT_ID(id) HID_USAGE(usage), HID_LOGICAL_MIN(0), HID_LOGICAL_MAX_N(0xFF, 2),     \
        HID_REPORT_SIZE(8), HID_REPORT_COUNT_N(n, 2), HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE)

static const uint8_t report_desc[] = {
    HID_USAGE_PAGE_N(HID_USAGE_PAGE_VENDOR, 2),
    HID_USAGE(0x01),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    FEATURE_REPORT(REPORT_ID_INFO, 0x01, sizeof(hid_info_t)),
    FEATURE_REPORT(REPORT_ID_STATUS, 0x02, sizeof(hid_status_t)),
    FEATURE_REPORT(REPORT_ID_COMMAND, 0x03, sizeof(hid_command_t)),
    FEATURE_REPORT(REPORT_ID_DEW, 0x04, sizeof(hid_dew_t)),
    FEATURE_REPORT(REPORT_ID_AUX, 0x05, sizeof(hid_aux_t)),
    FEATURE_REPORT(REPORT_ID_SCAN, 0x06, sizeof(hid_scan_t)),
    FEATURE_REPORT(REPORT_ID_TIME, 0x07, sizeof(hid_time_t)),
    FEATURE_REPORT(REPORT_ID_LABELS, 0x08, HID_LABELS_SIZE),
    HID_COLLECTION_END,
};

static const tusb_desc_device_t device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

#define EP_HID_IN 0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0, 100),
    // HID needs an interrupt IN endpoint even though all traffic uses feature reports.
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(report_desc), EP_HID_IN, 16, 10),
};

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&device_desc; }

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return config_desc;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return report_desc;
}

static const char *const strings[] = {"6l6", "Scope Hub"};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t buf[33];
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *s;
    size_t n;

    if (index == 0) {
        buf[1] = 0x0409; // English (US)
        n = 1;
    } else {
        if (index == 3) {
            pico_get_unique_board_id_string(serial, sizeof serial);
            s = serial;
        } else if (index <= sizeof strings / sizeof strings[0]) {
            s = strings[index - 1];
        } else {
            return NULL;
        }
        n = strlen(s);
        if (n > 32)
            n = 32;
        for (size_t i = 0; i < n; i++)
            buf[1 + i] = (uint8_t)s[i];
    }
    buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return buf;
}
