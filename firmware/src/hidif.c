#include "hidif.h"

#include <string.h>

#include "hardware/watchdog.h"
#include "hid_proto.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include "i2cbus.h"
#include "labels.h"

_Static_assert(sizeof(labels_t) == HID_LABELS_SIZE, "labels report layout");
#include "settings.h"
#include "tusb.h"

#define GPS_FRESH_US 3000000
#define REBOOT_DELAY_US 50000 // lets the SET_REPORT status stage finish first

static ports_t *g_ports;
static env_t *g_env;
static gps_t *g_gps;
static dew_t *g_dew;
static ledauto_t *g_led;
static bool g_watchdog_reset;
static bool save_pending;
static hid_command_t last = {.status = RES_PENDING};
static uint8_t pending_op;
static uint64_t pending_at_us;

void hidif_init(ports_t *ports, env_t *env, gps_t *gps, dew_t *dew, ledauto_t *led, bool watchdog_reset) {
    g_dew = dew;
    g_led = led;
    g_watchdog_reset = watchdog_reset;
    g_ports = ports;
    g_env = env;
    g_gps = gps;
}

static settings_ctx_t sctx(void) { return (settings_ctx_t){.ports = g_ports, .dew = g_dew, .led = g_led}; }

void hidif_poll(uint64_t now_us) {
    if (save_pending) {
        // Saving runs here, after the SET_REPORT has completed, because the
        // flash erase holds interrupts off for about 50 ms.
        settings_ctx_t c = sctx();
        save_pending = false;
        last.status = settings_save(&c) ? RES_OK : RES_FAILED;
    }
    if (!pending_op || now_us < pending_at_us)
        return;
    if (pending_op == CMD_BOOTSEL)
        reset_usb_boot(0, 0);
    watchdog_reboot(0, 0, 0);
    for (;;)
        tight_loop_contents();
}

static uint16_t fill_info(uint8_t *buf) {
    hid_info_t r = {0};
    r.proto_version = HID_PROTO_VERSION;
    r.dc_ports = DC_PORTS;
    r.usb_ports = USB_PORTS;
    strncpy(r.fw_version, FW_VERSION, sizeof r.fw_version - 1);
    pico_get_unique_board_id_string(r.serial, sizeof r.serial);
    memcpy(buf, &r, sizeof r);
    return sizeof r;
}

static uint16_t fill_status(uint8_t *buf) {
    const ports_t *p = g_ports;
    const env_t *e = g_env;
    const gps_t *g = g_gps;
    uint64_t now = time_us_64();
    hid_status_t r = {0};

    r.uptime_ms = (uint32_t)(now / 1000);
    bool fresh = g->seen && now - g->last_sentence_us < GPS_FRESH_US;
    r.flags = (p->local_5v_pin ? ST_LOCAL_5V_GOOD : 0) | (p->host_mounted ? ST_HOST_MOUNTED : 0) |
              (p->pca_present ? ST_PCA_PRESENT : 0) | (p->input.valid ? ST_INPUT_VALID : 0) |
              (e->sht_valid ? ST_SHT_VALID : 0) | (e->bmp_valid ? ST_BMP_VALID : 0) |
              (e->als_valid ? ST_ALS_VALID : 0) | (e->als_valid && e->als_saturated ? ST_ALS_SATURATED : 0) |
              (fresh ? ST_GPS_SEEN : 0) | (fresh && g->rmc_valid ? ST_GPS_RMC_VALID : 0) |
              (g->pps_count ? ST_PPS_SEEN : 0);
    r.led_pct = p->led_pct;
    r.gps_fix = fresh ? (uint8_t)g->fix_quality : 0;
    r.gps_sats = fresh ? (uint8_t)g->satellites : 0;
    r.dc_pwm_hz = p->dc_pwm_hz;
    r.led_pwm_hz = p->led_pwm_hz;
    r.in_volts = p->input.volts;
    r.in_amps = p->input.amps;

    for (int i = 0; i < DC_PORTS; i++) {
        hid_dc_t *d = &r.dc[i];
        uint8_t eff = ports_dc_effective(p, i);
        bool pwm = eff > 0 && eff < 100;
        float avg;
        bool avg_ok = ports_dc_avg_amps(p, i, &avg);
        d->duty = p->dc_duty[i];
        d->flags = (p->dc_on[i] ? DC_ON : 0) | (avg_ok ? DC_A_VALID : 0) | (p->dc_enabled[i] ? DC_ENABLED : 0) |
                   (p->dc_pwm[i] ? DC_PWM : 0);
        if (pwm) {
            d->volts = p->dc_volts_on[i];
            d->amps_on = p->dc_amps_on[i];
            d->flags |= p->dc_on_valid[i] ? DC_V_VALID | DC_AON_VALID : 0;
        } else {
            d->volts = p->dc_mon[i].volts;
            d->amps_on = p->dc_mon[i].amps;
            d->flags |= p->dc_mon[i].valid ? DC_V_VALID | DC_AON_VALID : 0;
        }
        d->amps = avg;
    }
    for (int i = 0; i < USB_PORTS; i++) {
        hid_usb_t *u = &r.usb[i];
        u->flags = (p->usb_attached[i] ? USB_ATTACHED : 0) | (p->usb_inhibit[i] ? 0 : USB_ENABLED) |
                   (p->usb_mon[i].valid ? USB_VALID : 0);
        u->volts = p->usb_mon[i].volts;
        u->amps = p->usb_mon[i].amps;
    }

    r.temp_c = e->temp_c;
    r.rh_pct = e->rh_pct;
    r.pressure_hpa = e->pressure_hpa;
    r.baro_temp_c = e->baro_temp_c;
    r.lux = e->lux;

    r.lat_deg = g->lat_deg;
    r.lon_deg = g->lon_deg;
    r.alt_m = g->alt_m;
    strncpy(r.utc, g->utc, sizeof r.utc - 1);
    strncpy(r.date, g->date, sizeof r.date - 1);
    r.pps_count = g->pps_count;
    r.pps_age_ms = g->pps_count ? (uint32_t)((now - g->pps_last_us) / 1000) : 0xFFFFFFFFu;

    memcpy(buf, &r, sizeof r);
    return sizeof r;
}

static uint16_t fill_dew(uint8_t *buf) {
    const dew_t *d = g_dew;
    hid_dew_t r = {
        .auto_mask = d->auto_mask,
        .flags = d->valid ? DEWF_VALID : 0,
        .auto_duty = d->auto_duty,
        .temp_offset_c = d->temp_offset_c,
        .start_c = d->start_c,
        .full_c = d->full_c,
        .min_pct = d->min_pct,
        .max_pct = d->max_pct,
        .fail_pct = d->fail_pct,
        .ambient_c = d->ambient_c,
        .dewpoint_c = d->dewpoint_c,
        .margin_c = d->margin_c,
    };
    memcpy(buf, &r, sizeof r);
    return sizeof r;
}

static bool in_range(float v, float lo, float hi) { return v >= lo && v <= hi; }

static uint16_t fill_aux(uint8_t *buf) {
    const env_t *e = g_env;
    const gps_t *g = g_gps;
    const ledauto_t *l = g_led;
    settings_ctx_t c = sctx();
    hid_aux_t r = {
        .led_auto = l->enabled,
        .als_range = e->als_range,
        .sats_in_view = g->sats_in_view,
        .sats_tracked = g->sats_tracked,
        .max_cnr = g->max_cnr,
        .flags = (settings_stored() ? AUX_SETTINGS_STORED : 0) | (settings_dirty(&c) ? AUX_SETTINGS_DIRTY : 0) |
                 (g_watchdog_reset ? AUX_WATCHDOG_RESET : 0) | (g_ports->pca_retained ? AUX_PCA_RETAINED : 0),
        .als_raw = e->als_raw,
        .led_min_pct = l->min_pct,
        .led_max_pct = l->max_pct,
        .led_dark_lux = l->dark_lux,
        .led_bright_lux = l->bright_lux,
        .als_lux_per_count = e->als_raw ? e->lux / (float)e->als_raw : 0.0f,
        .hdop = g->hdop,
    };
    memcpy(buf, &r, sizeof r);
    return sizeof r;
}

static uint16_t fill_time(uint8_t *buf) {
    uint64_t now = time_us_64(); // first, as close to the transfer as possible
    const gps_t *g = g_gps;
    hid_time_t r = {
        .dev_us = now,
        .pps_dev_us = g->pps_utc_dev_us,
        .pps_utc_s = g->pps_utc_s,
        .crystal_ppm = g->crystal_ppm,
        .pps_jitter_us = g->pps_jitter_us,
        .pps_count = g->pps_count,
        .pps_intervals = g->pps_intervals,
        .flags = (uint8_t)((g->pps_utc_valid ? TIME_UTC_VALID : 0) |
                           (g->pps_count && now - g->pps_last_us < 1500000 ? TIME_PPS_FRESH : 0)),
    };
    memcpy(buf, &r, sizeof r);
    return sizeof r;
}

static uint16_t fill_scan(uint8_t *buf) {
    hid_scan_t r = {0};
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_probe(i2c0, a))
            r.bus0[a / 8] |= (uint8_t)(1u << (a % 8));
        if (i2c_probe(i2c1, a))
            r.bus1[a / 8] |= (uint8_t)(1u << (a % 8));
    }
    memcpy(buf, &r, sizeof r);
    return sizeof r;
}

static uint8_t led_setting(uint8_t id, float v) {
    ledauto_t *l = g_led;
    switch (id) {
    case LED_MIN:
    case LED_MAX:
        if (!in_range(v, 0.0f, 100.0f))
            return RES_BAD_ARGS;
        *(id == LED_MIN ? &l->min_pct : &l->max_pct) = v;
        return RES_OK;
    case LED_DARK:
    case LED_BRIGHT:
        if (!in_range(v, 0.001f, 100000.0f))
            return RES_BAD_ARGS;
        *(id == LED_DARK ? &l->dark_lux : &l->bright_lux) = v;
        return RES_OK;
    default:
        return RES_BAD_ARGS;
    }
}

static uint8_t dew_setting(uint8_t id, float v) {
    dew_t *d = g_dew;
    switch (id) {
    case DEW_TEMP_OFFSET:
        if (!in_range(v, -20.0f, 20.0f))
            return RES_BAD_ARGS;
        d->temp_offset_c = v;
        return RES_OK;
    case DEW_START:
        if (!in_range(v, 0.0f, 30.0f))
            return RES_BAD_ARGS;
        d->start_c = v;
        return RES_OK;
    case DEW_FULL:
        if (!in_range(v, -10.0f, 30.0f))
            return RES_BAD_ARGS;
        d->full_c = v;
        return RES_OK;
    case DEW_MIN:
    case DEW_MAX:
    case DEW_FAIL:
        if (!in_range(v, 0.0f, 100.0f))
            return RES_BAD_ARGS;
        *(id == DEW_MIN ? &d->min_pct : id == DEW_MAX ? &d->max_pct : &d->fail_pct) = v;
        return RES_OK;
    default:
        return RES_BAD_ARGS;
    }
}

static uint8_t run_command(const hid_command_t *c, float *applied) {
    ports_t *p = g_ports;
    int port = (int)c->port - 1;
    float v = c->value;
    *applied = v;

    switch (c->op) {
    case CMD_DC:
        if (!(v >= 0.0f && v <= 100.0f))
            return RES_BAD_ARGS;
        *applied = (float)(int)(v + 0.5f);
        if (port >= 0 && port < DC_PORTS)
            dew_set_auto(g_dew, p, port, false); // a manual duty ends auto control
        return ports_set_dc(p, port, (int)*applied) ? RES_OK : RES_FAILED;
    case CMD_DC_ENABLE:
        if (port < 0 || port >= DC_PORTS)
            return RES_BAD_ARGS;
        return ports_set_dc_enabled(p, port, v != 0.0f) ? RES_OK : RES_FAILED;
    case CMD_DC_PWM:
        if (port < 0 || port >= DC_PORTS)
            return RES_BAD_ARGS;
        if (v == 0.0f)
            dew_set_auto(g_dew, p, port, false); // auto control needs PWM
        return ports_set_dc_pwm(p, port, v != 0.0f) ? RES_OK : RES_FAILED;
    case CMD_DC_AUTO:
        if (port < 0 || port >= DC_PORTS)
            return RES_BAD_ARGS;
        return dew_set_auto(g_dew, p, port, v != 0.0f) ? RES_OK : RES_FAILED;
    case CMD_DEW_SET:
        return dew_setting(c->port, v);
    case CMD_USB:
        if (port < 0 || port >= USB_PORTS)
            return RES_BAD_ARGS;
        return ports_set_usb(p, port, v != 0.0f) ? RES_OK : RES_FAILED;
    case CMD_LED:
        if (!(v >= 0.0f && v <= 100.0f))
            return RES_BAD_ARGS;
        *applied = (float)(int)(v + 0.5f);
        g_led->enabled = false; // a manual level ends auto dimming
        return ports_set_led_brightness(p, (int)*applied) ? RES_OK : RES_FAILED;
    case CMD_LED_AUTO:
        g_led->enabled = v != 0.0f;
        return RES_OK;
    case CMD_LED_SET:
        return led_setting(c->port, v);
    case CMD_SAVE:
        save_pending = true;
        return RES_PENDING; // hidif_poll writes the result after the flash write
    case CMD_DEFAULTS:
        dew_init_settings(g_dew);
        ledauto_init(g_led);
        for (int i = 0; i < USB_PORTS; i++)
            ports_set_usb(p, i, true);
        ports_set_dc_pwm_hz(p, 1.0f);
        return ports_set_led_brightness(p, 10) ? RES_OK : RES_FAILED;
    case CMD_DC_PWM_HZ:
        if (!ports_set_dc_pwm_hz(p, v))
            return RES_BAD_ARGS;
        *applied = p->dc_pwm_hz;
        return RES_OK;
    case CMD_GPS_RESET:
        if (v == 0.0f)
            gps_reset_pin(g_gps);
        else if (v == 1.0f)
            gps_cold_start(g_gps);
        else
            return RES_BAD_ARGS;
        return RES_OK;
    case CMD_REBOOT:
    case CMD_BOOTSEL:
        pending_op = c->op;
        pending_at_us = time_us_64() + REBOOT_DELAY_US;
        return RES_OK;
    default:
        return RES_UNKNOWN_OP;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void)instance;
    if (report_type != HID_REPORT_TYPE_FEATURE)
        return 0;
    switch (report_id) {
    case REPORT_ID_INFO:
        return reqlen >= sizeof(hid_info_t) ? fill_info(buffer) : 0;
    case REPORT_ID_STATUS:
        return reqlen >= sizeof(hid_status_t) ? fill_status(buffer) : 0;
    case REPORT_ID_LABELS:
        if (reqlen < sizeof(labels_t))
            return 0;
        memcpy(buffer, labels_get(), sizeof(labels_t));
        return sizeof(labels_t);
    case REPORT_ID_TIME:
        return reqlen >= sizeof(hid_time_t) ? fill_time(buffer) : 0;
    case REPORT_ID_SCAN:
        return reqlen >= sizeof(hid_scan_t) ? fill_scan(buffer) : 0;
    case REPORT_ID_AUX:
        return reqlen >= sizeof(hid_aux_t) ? fill_aux(buffer) : 0;
    case REPORT_ID_DEW:
        return reqlen >= sizeof(hid_dew_t) ? fill_dew(buffer) : 0;
    case REPORT_ID_COMMAND:
        if (reqlen < sizeof last)
            return 0;
        memcpy(buffer, &last, sizeof last);
        return sizeof last;
    default:
        return 0; // stalls the request
    }
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, const uint8_t *buffer,
                           uint16_t bufsize) {
    (void)instance;
    if (report_type == HID_REPORT_TYPE_FEATURE && report_id == REPORT_ID_LABELS && bufsize >= sizeof(labels_t)) {
        labels_t l;
        memcpy(&l, buffer, sizeof l);
        labels_set(&l);
        return;
    }
    if (report_type != HID_REPORT_TYPE_FEATURE || report_id != REPORT_ID_COMMAND || bufsize < sizeof(hid_command_t))
        return;
    hid_command_t c;
    float applied;
    memcpy(&c, buffer, sizeof c);
    c.status = run_command(&c, &applied);
    c.value = applied;
    last = c;
}
