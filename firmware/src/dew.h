// Open-loop dew-heater control from the SHT45.
//
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "env.h"
#include "ports.h"

typedef struct {
    float temp_offset_c;
    float start_c;
    float full_c;
    float min_pct;
    float max_pct;
    float fail_pct;
    uint8_t auto_mask;

    bool valid;
    float ambient_c;
    float dewpoint_c;
    float margin_c;
    uint8_t auto_duty;
} dew_t;

void dew_init(dew_t *d);

void dew_init_settings(dew_t *d);

void dew_poll(dew_t *d, const env_t *env, ports_t *ports, uint64_t now_us);

bool dew_set_auto(dew_t *d, ports_t *ports, int port, bool on);
