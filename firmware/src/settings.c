#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

#define SETTINGS_MAGIC 0x42554853u // "SHUB"
#define SETTINGS_VERSION 3
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

_Static_assert(sizeof(settings_t) <= FLASH_PAGE_SIZE, "settings fit one flash page");

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));
    }
    return ~c;
}

static const settings_t *stored(void) { return (const settings_t *)(XIP_BASE + SETTINGS_OFFSET); }

static bool valid(const settings_t *s) {
    return s->magic == SETTINGS_MAGIC && s->version == SETTINGS_VERSION && s->size == sizeof(settings_t) &&
           s->crc == crc32((const uint8_t *)s, offsetof(settings_t, crc));
}

bool settings_stored(void) { return valid(stored()); }

bool settings_load(settings_t *s) {
    if (!valid(stored()))
        return false;
    memcpy(s, stored(), sizeof *s);
    return true;
}

void settings_capture(const settings_ctx_t *c, settings_t *s) {
    const ports_t *p = c->ports;
    const dew_t *d = c->dew;
    const ledauto_t *l = c->led;
    memset(s, 0, sizeof *s);
    s->magic = SETTINGS_MAGIC;
    s->version = SETTINGS_VERSION;
    s->size = sizeof *s;
    for (int i = 0; i < DC_PORTS; i++)
        s->dc_duty[i] = (d->auto_mask & (1u << i)) ? 0 : p->dc_duty[i];
    for (int i = 0; i < DC_PORTS; i++)
        s->dc_on_mask |= (uint8_t)(p->dc_enabled[i] << i);
    for (int i = 0; i < DC_PORTS; i++)
        s->dc_pwm_mask |= (uint8_t)(p->dc_pwm[i] << i);
    for (int i = 0; i < USB_PORTS; i++)
        s->usb_off_mask |= (uint8_t)(p->usb_inhibit[i] << i);
    s->dew_auto_mask = d->auto_mask;
    s->led_pct = l->enabled ? 0 : p->led_pct;
    s->led_auto = l->enabled;
    s->dc_pwm_hz = p->dc_pwm_hz;
    s->dew_temp_offset_c = d->temp_offset_c;
    s->dew_start_c = d->start_c;
    s->dew_full_c = d->full_c;
    s->dew_min_pct = d->min_pct;
    s->dew_max_pct = d->max_pct;
    s->dew_fail_pct = d->fail_pct;
    s->led_min_pct = l->min_pct;
    s->led_max_pct = l->max_pct;
    s->led_dark_lux = l->dark_lux;
    s->led_bright_lux = l->bright_lux;
    s->crc = crc32((const uint8_t *)s, offsetof(settings_t, crc));
}

void settings_apply(const settings_ctx_t *c, const settings_t *s) {
    ports_t *p = c->ports;
    dew_t *d = c->dew;
    ledauto_t *l = c->led;

    d->temp_offset_c = s->dew_temp_offset_c;
    d->start_c = s->dew_start_c;
    d->full_c = s->dew_full_c;
    d->min_pct = s->dew_min_pct;
    d->max_pct = s->dew_max_pct;
    d->fail_pct = s->dew_fail_pct;
    l->enabled = s->led_auto;
    l->min_pct = s->led_min_pct;
    l->max_pct = s->led_max_pct;
    l->dark_lux = s->led_dark_lux;
    l->bright_lux = s->led_bright_lux;
    ports_set_dc_pwm_hz(p, s->dc_pwm_hz);
    if (!s->led_auto)
        ports_set_led_brightness(p, s->led_pct);

    for (int i = 0; i < USB_PORTS; i++)
        ports_set_usb(p, i, !(s->usb_off_mask & (1u << i)));

    for (int i = 0; i < DC_PORTS; i++) {
        bool is_auto = s->dew_auto_mask & (1u << i);
        bool saved_on = s->dc_on_mask & (1u << i);
        bool saved_pwm = s->dc_pwm_mask & (1u << i);
        if (is_auto) {
            dew_set_auto(d, p, i, true);
        } else {
            ports_set_dc(p, i, s->dc_duty[i]);
            if (!p->pca_retained || saved_pwm)
                ports_set_dc_pwm(p, i, saved_pwm);
        }
        // After an MCU-only reset, caused by a firmware update or a
        // watchdog reset, retain existing pca config
        bool steady = !is_auto && (!saved_pwm || s->dc_duty[i] == 100);
        if (!p->pca_retained || !steady)
            ports_set_dc_enabled(p, i, saved_on);
    }
}

bool settings_save(const settings_ctx_t *c) {
    static uint8_t page[FLASH_PAGE_SIZE];
    settings_t s;
    settings_capture(c, &s);
    memset(page, 0xFF, sizeof page);
    memcpy(page, &s, sizeof s);

    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
    return valid(stored()) && memcmp(stored(), &s, sizeof s) == 0;
}

bool settings_dirty(const settings_ctx_t *c) {
    settings_t live;
    settings_capture(c, &live);
    return !valid(stored()) || memcmp(stored(), &live, sizeof live) != 0;
}
