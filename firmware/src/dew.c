#include "dew.h"

#include <math.h>

#define DEW_PERIOD_US 1000000

// Magnus formula constants over water (Sonntag 1990), valid -45..60 C.
#define MAGNUS_B 17.62f
#define MAGNUS_C 243.12f

void dew_init_settings(dew_t *d) {
    d->temp_offset_c = 0.0f;
    d->start_c = 6.0f;
    d->full_c = 1.0f;
    d->min_pct = 10.0f;
    d->max_pct = 100.0f;
    d->fail_pct = 50.0f;
}

void dew_init(dew_t *d) {
    *d = (dew_t){0};
    dew_init_settings(d);
}

static float dew_point(float t, float rh) {
    if (rh < 0.1f)
        rh = 0.1f;
    float g = logf(rh / 100.0f) + MAGNUS_B * t / (MAGNUS_C + t);
    return MAGNUS_C * g / (MAGNUS_B - g);
}

static uint8_t duty_for(const dew_t *d) {
    float pct;
    if (!d->valid)
        pct = d->fail_pct;
    else if (d->margin_c >= d->start_c)
        pct = 0.0f;
    else if (d->margin_c <= d->full_c || d->start_c <= d->full_c)
        pct = d->max_pct;
    else
        pct = d->min_pct + (d->max_pct - d->min_pct) * (d->start_c - d->margin_c) / (d->start_c - d->full_c);
    if (pct < 0.0f)
        pct = 0.0f;
    if (pct > d->max_pct)
        pct = d->max_pct;
    return (uint8_t)(pct + 0.5f);
}

void dew_poll(dew_t *d, const env_t *env, ports_t *ports, uint64_t now_us) {
    static uint64_t next_us;
    if (now_us < next_us)
        return;
    next_us = now_us + DEW_PERIOD_US;

    d->valid = env->sht_valid;
    if (d->valid) {
        d->ambient_c = env->temp_c + d->temp_offset_c;
        d->dewpoint_c = dew_point(env->temp_c, env->rh_pct);
        d->margin_c = d->ambient_c - d->dewpoint_c;
    }
    d->auto_duty = duty_for(d);
    for (int i = 0; i < DC_PORTS; i++) {
        if ((d->auto_mask & (1u << i)) && ports->dc_duty[i] != d->auto_duty)
            ports_set_dc(ports, i, d->auto_duty);
    }
}

bool dew_set_auto(dew_t *d, ports_t *ports, int port, bool on) {
    if (port < 0 || port >= DC_PORTS)
        return false;
    if (on) {
        // Auto control works through PWM, so it switches PWM on.
        d->auto_mask |= (uint8_t)(1u << port);
        return ports_set_dc(ports, port, d->auto_duty) && ports_set_dc_pwm(ports, port, true);
    }
    d->auto_mask &= (uint8_t)~(1u << port);
    return true;
}
