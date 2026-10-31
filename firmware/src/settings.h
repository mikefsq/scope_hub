// User settings stored in the last 4 KB sector of the QSPI flash.
//
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dew.h"
#include "ledauto.h"
#include "ports.h"

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t dc_duty[6];
    uint8_t usb_off_mask;
    uint8_t dew_auto_mask;
    uint8_t led_pct;
    uint8_t led_auto;
    uint8_t dc_on_mask;
    uint8_t dc_pwm_mask;
    float dc_pwm_hz;
    float dew_temp_offset_c, dew_start_c, dew_full_c, dew_min_pct, dew_max_pct, dew_fail_pct;
    float led_min_pct, led_max_pct, led_dark_lux, led_bright_lux;
    uint32_t crc;
} settings_t;

typedef struct {
    ports_t *ports;
    dew_t *dew;
    ledauto_t *led;
} settings_ctx_t;

bool settings_load(settings_t *s);
void settings_capture(const settings_ctx_t *c, settings_t *s);
void settings_apply(const settings_ctx_t *c, const settings_t *s);
bool settings_save(const settings_ctx_t *c);
bool settings_dirty(const settings_ctx_t *c);
bool settings_stored(void);
