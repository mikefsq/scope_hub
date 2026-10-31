// 12 V input monitor, six switched DC outputs,
// six USB-C downstream ports and the indicator LEDs.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "ina226.h"

typedef struct {
    ina226_t input;
    ina226_t dc_mon[DC_PORTS];
    bool dc_enabled[DC_PORTS];
    bool dc_pwm[DC_PORTS];
    uint8_t dc_duty[DC_PORTS];
    bool dc_on[DC_PORTS];
    uint64_t dc_edge_us[DC_PORTS];
    uint64_t dc_read_at_us[DC_PORTS];
    bool dc_on_valid[DC_PORTS];
    float dc_volts_on[DC_PORTS];
    float dc_amps_on[DC_PORTS];
    float dc_pwm_hz;

    ina226_t usb_mon[USB_PORTS];
    bool usb_attached[USB_PORTS];
    bool usb_inhibit[USB_PORTS];

    bool pca_present;
    bool pca_retained; // the PCA9685 kept its outputs through an MCU-only reset
    float led_pwm_hz;  // PCA9685 frequency
    uint8_t led_pct;   // brightness of every lit indicator LED, 0..100
    bool local_5v_pin; // high while the local 12 V to 5 V supply is good
    bool host_mounted; // USB device configured by the host
} ports_t;

void ports_init(ports_t *p);

void ports_poll(ports_t *p, uint64_t now_us);

bool ports_set_dc(ports_t *p, int port, int duty_pct);
bool ports_set_dc_enabled(ports_t *p, int port, bool on);
bool ports_set_dc_pwm(ports_t *p, int port, bool on);

// The mode dependent duty cycle for the port
static inline uint8_t ports_dc_effective(const ports_t *p, int port) {
    if (!p->dc_enabled[port])
        return 0;
    return p->dc_pwm[port] ? p->dc_duty[port] : 100;
}

bool ports_set_dc_pwm_hz(ports_t *p, float hz);
bool ports_set_usb(ports_t *p, int port, bool enabled);

bool ports_set_led_brightness(ports_t *p, int pct);

bool ports_dc_avg_amps(const ports_t *p, int port, float *amps);
