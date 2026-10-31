// Automatic indicator-LED brightness from the VEML7700 lux reading.
//
// Brightness follows log10(lux). Setting a
// manual brightness turns auto mode off.
#pragma once

#include <stdbool.h>

#include "env.h"
#include "ports.h"

typedef struct {
    bool enabled;
    float min_pct;
    float max_pct;
    float dark_lux;
    float bright_lux;
} ledauto_t;

void ledauto_init(ledauto_t *l);

void ledauto_poll(ledauto_t *l, const env_t *env, ports_t *ports, uint64_t now_us);
