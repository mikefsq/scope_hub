// Environment sensors on I2C1: SHT45 (temperature, humidity), BMP581 (pressure,
// temperature) and VEML7700 (ambient light).
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool sht_present, sht_valid;
    float temp_c, rh_pct;

    bool bmp_present, bmp_valid;
    float baro_temp_c, pressure_hpa;

    bool als_present, als_valid;
    uint16_t als_raw;
    bool als_saturated;
    float lux;
    uint8_t als_range;
    bool als_settling;
} env_t;

void env_init(env_t *env);

void env_poll(env_t *env, uint64_t now_us);
