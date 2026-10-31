#include "ledauto.h"

#include <math.h>

#define LEDAUTO_PERIOD_US 1000000

void ledauto_init(ledauto_t *l) {
    *l = (ledauto_t){
        .enabled = false,
        .min_pct = 1.0f,
        .max_pct = 50.0f,
        .dark_lux = 0.1f,
        .bright_lux = 500.0f,
    };
}

void ledauto_poll(ledauto_t *l, const env_t *env, ports_t *ports, uint64_t now_us) {
    static uint64_t next_us;
    if (now_us < next_us)
        return;
    next_us = now_us + LEDAUTO_PERIOD_US;
    if (!l->enabled || !env->als_valid)
        return;

    float pct;
    if (env->lux <= l->dark_lux || l->bright_lux <= l->dark_lux)
        pct = l->min_pct;
    else if (env->lux >= l->bright_lux)
        pct = l->max_pct;
    else {
        float f = log10f(env->lux / l->dark_lux) / log10f(l->bright_lux / l->dark_lux);
        pct = l->min_pct + (l->max_pct - l->min_pct) * f;
    }
    int want = (int)(pct + 0.5f);
    if (want != ports->led_pct)
        ports_set_led_brightness(ports, want);
}
