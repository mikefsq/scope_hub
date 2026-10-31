// USB HID protocol between the scope_hub firmware and the host driver.
//
// One vendor-defined HID interface (usage page 0xFF00) carries three feature
// reports. All fields are little-endian and packed.
#pragma once

#include <stdint.h>

#define HID_PROTO_VERSION 1

// USB IDs. 1209:0001 is the pid.codes test PID, for development only.
#define USB_VID 0x1209
#define USB_PID 0x0001

#define REPORT_ID_INFO 1    // feature, read
#define REPORT_ID_STATUS 2  // feature, read
#define REPORT_ID_COMMAND 3 // feature, write a command, read its result
#define REPORT_ID_DEW 4     // feature, read
#define REPORT_ID_AUX 5     // feature, read
#define REPORT_ID_SCAN 6    // feature, read: runs an I2C scan, about 30 ms
#define REPORT_ID_TIME 7    // feature, read: board time and the latest UTC-labeled PPS edge
#define REPORT_ID_LABELS 8  // feature, read and write: port names, stored in flash on write
// Report 8 is labels_t from labels.h: 6 DC then 6 USB names of 16 bytes each,
// NUL-terminated. Writing it stores the names in flash within about 50 ms.
#define HID_LABELS_SIZE 192

typedef struct __attribute__((packed)) {
    uint8_t proto_version;
    uint8_t dc_ports;
    uint8_t usb_ports;
    uint8_t reserved;
    char fw_version[12]; // NUL-terminated
    char serial[20];     // RP2350 unique ID as hex, NUL-terminated
} hid_info_t;

// hid_status_t.flags
#define ST_LOCAL_5V_GOOD (1u << 0)
#define ST_HOST_MOUNTED (1u << 1)
#define ST_PCA_PRESENT (1u << 2)
#define ST_INPUT_VALID (1u << 3)
#define ST_SHT_VALID (1u << 4)
#define ST_BMP_VALID (1u << 5)
#define ST_ALS_VALID (1u << 6)
#define ST_ALS_SATURATED (1u << 7)
#define ST_GPS_SEEN (1u << 8)      // NMEA received in the last 3 s
#define ST_GPS_RMC_VALID (1u << 9) // RMC status 'A'
#define ST_PPS_SEEN (1u << 10)     // at least one PPS edge since boot

// hid_dc_t.flags
#define DC_ON (1u << 0)       // enable output currently high
#define DC_V_VALID (1u << 1)
#define DC_A_VALID (1u << 2)  // average current
#define DC_AON_VALID (1u << 3)
#define DC_ENABLED (1u << 4)  // the output's on/off switch is on
#define DC_PWM (1u << 5)      // PWM on: the output runs at its duty cycle; off holds it at 100%

typedef struct __attribute__((packed)) {
    uint8_t duty; // duty cycle percent, applied while DC_ENABLED is set
    uint8_t flags;
    uint8_t reserved[2];
    float volts;   // during PWM, the last reading taken while on
    float amps;    // average over a PWM period
    float amps_on; // current while on
} hid_dc_t;

// hid_usb_t.flags
#define USB_ATTACHED (1u << 0)
#define USB_ENABLED (1u << 1) // not inhibited by the firmware
#define USB_VALID (1u << 2)   // monitor reading valid

typedef struct __attribute__((packed)) {
    uint8_t flags;
    uint8_t reserved[3];
    float volts;
    float amps;
} hid_usb_t;

typedef struct __attribute__((packed)) {
    uint32_t uptime_ms;
    uint32_t flags;
    uint8_t led_pct;
    uint8_t gps_fix;  // GGA fix quality
    uint8_t gps_sats; // GGA satellites used
    uint8_t reserved;
    float dc_pwm_hz;
    float led_pwm_hz;
    float in_volts;
    float in_amps;
    hid_dc_t dc[6];
    hid_usb_t usb[6];
    float temp_c;
    float rh_pct;
    float pressure_hpa;
    float baro_temp_c;
    float lux;
    double lat_deg; // valid while gps_fix > 0
    double lon_deg;
    float alt_m;
    char utc[12];  // hhmmss.ss, NUL-terminated
    char date[8];  // ddmmyy, NUL-terminated
    uint32_t pps_count;
    uint32_t pps_age_ms;
} hid_status_t;

// Command ops
#define CMD_DC 1       // port 1..6, value = duty cycle percent 0..100; does not switch the output
#define CMD_DC_ENABLE 14 // port 1..6, value 1 = output on, 0 = off
#define CMD_DC_PWM 15    // port 1..6, value 1 = run at the duty cycle, 0 = steady 100% and dew auto off
#define CMD_USB 2      // port 1..6, value = 1 enable, 0 force VBUS off
#define CMD_LED 3      // value = brightness percent 0..100
#define CMD_DC_PWM_HZ 4 // value = 0.1..10 Hz
#define CMD_REBOOT 5
#define CMD_BOOTSEL 6  // reboot into the USB boot ROM for flashing
#define CMD_GPS_RESET 7 // value 0 = pulse RESET_N, 1 = UBX cold start (clears ephemeris and almanac)
#define CMD_DC_AUTO 8   // port 1..6, value 1 = dew auto control, 0 = manual at the current duty
#define CMD_DEW_SET 9   // port = DEW_* setting id, value = new setting
#define CMD_LED_AUTO 10 // value 1 = brightness follows the lux sensor, 0 = manual at the current level
#define CMD_LED_SET 11  // port = LED_* setting id, value = new setting
#define CMD_SAVE 12     // write the live settings to flash
#define CMD_DEFAULTS 13 // restore default settings in RAM; CMD_SAVE makes them permanent

// CMD_LED_SET setting ids
#define LED_MIN 1    // brightness % at or below the dark level, 0..100
#define LED_MAX 2    // brightness % at or above the bright level, 0..100
#define LED_DARK 3   // lux, 0.001..100000
#define LED_BRIGHT 4 // lux, 0.001..100000

// hid_aux_t.flags
#define AUX_SETTINGS_STORED (1u << 0) // a valid record is in flash
#define AUX_SETTINGS_DIRTY (1u << 1)  // live settings differ from flash
#define AUX_WATCHDOG_RESET (1u << 2)  // the last reset came from the watchdog
#define AUX_PCA_RETAINED (1u << 3)    // DC outputs survived the last MCU reset

typedef struct __attribute__((packed)) {
    uint8_t led_auto;
    uint8_t als_range;    // 0 = most sensitive of 9 gain and integration-time steps
    uint8_t sats_in_view; // GSV
    uint8_t sats_tracked; // GSV satellites with a signal
    uint8_t max_cnr;      // dB-Hz
    uint8_t flags;
    uint16_t als_raw;
    float led_min_pct;
    float led_max_pct;
    float led_dark_lux;
    float led_bright_lux;
    float als_lux_per_count;
    float hdop;
} hid_aux_t;

// CMD_DEW_SET setting ids
#define DEW_TEMP_OFFSET 1 // C added to the SHT45 temperature, -20..20
#define DEW_START 2       // margin C where heating starts, 0..30
#define DEW_FULL 3        // margin C where max duty is reached, -10..30
#define DEW_MIN 4         // duty % at the start margin, 0..100
#define DEW_MAX 5         // duty % cap, 0..100
#define DEW_FAIL 6        // duty % while the sensor has no reading, 0..100

// hid_dew_t.flags
#define DEWF_VALID (1u << 0) // ambient, dew point and margin are from a valid reading

typedef struct __attribute__((packed)) {
    uint8_t auto_mask; // bit n: DC port n+1 under auto control
    uint8_t flags;
    uint8_t auto_duty; // duty % applied to auto ports
    uint8_t reserved;
    float temp_offset_c;
    float start_c;
    float full_c;
    float min_pct;
    float max_pct;
    float fail_pct;
    float ambient_c;
    float dewpoint_c;
    float margin_c;
} hid_dew_t;

// Command result codes
#define RES_OK 0
#define RES_BAD_ARGS 1
#define RES_FAILED 2
#define RES_UNKNOWN_OP 3
#define RES_PENDING 0xFF // no command received yet

// Written by the host to run a command. Read back to get the result of the
// last command: seq and op echo the command, status holds the result code,
// value holds the applied value where one applies.
typedef struct __attribute__((packed)) {
    uint8_t seq;
    uint8_t op;
    uint8_t port;
    uint8_t status;
    float value;
} hid_command_t;

_Static_assert(sizeof(hid_info_t) == 36, "info layout");
_Static_assert(sizeof(hid_dc_t) == 16, "dc layout");
_Static_assert(sizeof(hid_usb_t) == 12, "usb layout");
_Static_assert(sizeof(hid_command_t) == 8, "command layout");
_Static_assert(sizeof(hid_status_t) == 264, "status layout");
_Static_assert(sizeof(hid_dew_t) == 40, "dew layout");
_Static_assert(sizeof(hid_aux_t) == 32, "aux layout");

// Addresses 0x08..0x77 that ACK a one-byte read: bit (a % 8) of byte a / 8.
// Devices that refuse a read with nothing to send, such as the SHT45, do not appear.
typedef struct __attribute__((packed)) {
    uint8_t bus0[16]; // I2C0
    uint8_t bus1[16]; // I2C1
} hid_scan_t;
_Static_assert(sizeof(hid_scan_t) == 32, "scan layout");

// hid_time_t.flags
#define TIME_UTC_VALID (1u << 0) // pps_utc_s labels the edge at pps_dev_us
#define TIME_PPS_FRESH (1u << 1) // the last PPS edge came less than 1.5 s ago

// The board's 1 us system timer runs from its crystal. UTC at dev_us is
// pps_utc_s + (dev_us - pps_dev_us) / (1 + crystal_ppm * 1e-6) microseconds.
// dev_us is read while the report is filled, inside the USB control transfer.
typedef struct __attribute__((packed)) {
    uint64_t dev_us;
    uint64_t pps_dev_us;
    int64_t pps_utc_s;
    float crystal_ppm;
    float pps_jitter_us;
    uint32_t pps_count;
    uint16_t pps_intervals;
    uint8_t flags;
    uint8_t reserved;
} hid_time_t;
_Static_assert(sizeof(hid_time_t) == 40, "time layout");
