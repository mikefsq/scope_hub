#include "cmd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#define CMD_LINE_MAX 96

static void reply_ok(void) { printf("{\"ok\":true}\n"); }

static void reply_err(const char *msg) { printf("{\"ok\":false,\"err\":\"%s\"}\n", msg); }

// Prints a number, or null when the reading is not valid.
static void num(const char *fmt, bool valid, double v) {
    if (valid)
        printf(fmt, v);
    else
        printf("null");
}

static void monitor(const ina226_t *m) {
    printf("\"v\":");
    num("%.3f", m->valid, m->volts);
    printf(",\"a\":");
    num("%.4f", m->valid, m->amps);
}

static void cmd_id(void) {
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(serial, sizeof serial);
    printf("{\"ok\":true,\"id\":\"scope_hub\",\"fw\":\"%s\",\"serial\":\"%s\"}\n", FW_VERSION, serial);
}

static void cmd_status(cmd_ctx_t *c) {
    const ports_t *p = c->ports;
    const env_t *e = c->env;
    const gps_t *g = c->gps;
    uint64_t now = time_us_64();

    printf("{\"ok\":true,\"uptime_ms\":%llu", (unsigned long long)(now / 1000));
    printf(",\"local5v_pin\":%s,\"host\":%s,\"led\":%d", p->local_5v_pin ? "true" : "false",
           p->host_mounted ? "true" : "false", p->led_pct);
    printf(",\"dc_pwm_hz\":%.2f,\"led_pwm_hz\":%.0f", p->dc_pwm_hz, p->led_pwm_hz);

    printf(",\"in\":{");
    monitor(&p->input);
    printf("}");

    printf(",\"dc\":[");
    for (int i = 0; i < DC_PORTS; i++) {
        // v and a: during PWM, the last reading taken while on and the
        // duty-weighted average current. Otherwise the plain reading.
        bool pwm = ports_dc_effective(p, i) > 0 && ports_dc_effective(p, i) < 100;
        float avg;
        bool avg_ok = ports_dc_avg_amps(p, i, &avg);
        printf("%s{\"port\":%d,\"duty\":%d,\"on\":%s,\"v\":", i ? "," : "", i + 1, p->dc_duty[i],
               p->dc_on[i] ? "true" : "false");
        if (pwm)
            num("%.3f", p->dc_on_valid[i], p->dc_volts_on[i]);
        else
            num("%.3f", p->dc_mon[i].valid, p->dc_mon[i].volts);
        printf(",\"a\":");
        num("%.4f", avg_ok, avg);
        printf(",\"a_on\":");
        num("%.4f", pwm ? p->dc_on_valid[i] : p->dc_mon[i].valid, pwm ? p->dc_amps_on[i] : p->dc_mon[i].amps);
        printf("}");
    }
    printf("]");

    printf(",\"usb\":[");
    for (int i = 0; i < USB_PORTS; i++) {
        printf("%s{\"port\":%d,\"attached\":%s,\"enabled\":%s,", i ? "," : "", i + 1,
               p->usb_attached[i] ? "true" : "false", p->usb_inhibit[i] ? "false" : "true");
        monitor(&p->usb_mon[i]);
        printf("}");
    }
    printf("]");

    printf(",\"env\":{\"t\":");
    num("%.2f", e->sht_valid, e->temp_c);
    printf(",\"rh\":");
    num("%.1f", e->sht_valid, e->rh_pct);
    printf(",\"p_hpa\":");
    num("%.2f", e->bmp_valid, e->pressure_hpa);
    printf(",\"t_baro\":");
    num("%.2f", e->bmp_valid, e->baro_temp_c);
    printf(",\"lux\":");
    num("%.4f", e->als_valid, e->lux);
    printf(",\"lux_sat\":%s}", e->als_valid && e->als_saturated ? "true" : "false");

    bool fresh = g->seen && now - g->last_sentence_us < 3000000;
    bool fix = fresh && g->fix_quality > 0;
    printf(",\"gps\":{\"seen\":%s,\"fix\":%d,\"sats\":%d,\"lat\":", fresh ? "true" : "false",
           fresh ? g->fix_quality : 0, fresh ? g->satellites : 0);
    num("%.7f", fix, g->lat_deg);
    printf(",\"lon\":");
    num("%.7f", fix, g->lon_deg);
    printf(",\"alt_m\":");
    num("%.1f", fix, g->alt_m);
    printf(",\"utc\":\"%s\",\"date\":\"%s\",\"pps\":%lu,\"pps_age_ms\":", g->utc, g->date,
           (unsigned long)g->pps_count);
    num("%.0f", g->pps_count > 0, (double)(now - g->pps_last_us) / 1000.0);
    printf("}}\n");
}

static void scan_bus(i2c_inst_t *bus) {
    bool first = true;
    printf("[");
    for (uint8_t a = 0x08; a < 0x78; a++) {
        uint8_t b;
        if (i2c_read_timeout_us(bus, a, &b, 1, false, 2000) == 1) {
            printf("%s\"0x%02x\"", first ? "" : ",", a);
            first = false;
        }
    }
    printf("]");
}

static void cmd_scan(void) {
    printf("{\"ok\":true,\"i2c0\":");
    scan_bus(i2c0);
    printf(",\"i2c1\":");
    scan_bus(i2c1);
    printf("}\n");
}

// Parses a decimal number argument such as 12 or 12.5.
static bool parse_float(const char *s, float *out) {
    char *end;
    if (!s)
        return false;
    float v = strtof(s, &end);
    if (end == s || *end)
        return false;
    *out = v;
    return true;
}

// Parses a whole decimal integer argument.
static bool parse_int(const char *s, int *out) {
    char *end;
    if (!s)
        return false;
    long v = strtol(s, &end, 10);
    if (end == s || *end)
        return false;
    *out = (int)v;
    return true;
}

static void run(cmd_ctx_t *c, char *line) {
    for (char *s = line; *s; s++)
        *s = (char)toupper((unsigned char)*s);
    char *verb = strtok(line, " \t");
    char *a1 = strtok(NULL, " \t");
    char *a2 = strtok(NULL, " \t");
    int port, val;
    float f;

    if (!verb)
        return;
    if (strcmp(verb, "ID") == 0) {
        cmd_id();
    } else if (strcmp(verb, "STATUS") == 0) {
        cmd_status(c);
    } else if (strcmp(verb, "SCAN") == 0) {
        cmd_scan();
    } else if (strcmp(verb, "DC") == 0) {
        // DC <1-6> <0-100>, rounded to whole percent
        if (!parse_int(a1, &port) || !parse_float(a2, &f) || !(f >= 0.0f && f <= 100.0f))
            reply_err("usage: DC <1-6> <0-100>");
        else if (!ports_set_dc(c->ports, port - 1, (int)(f + 0.5f)) ||
                 !ports_set_dc_pwm(c->ports, port - 1, f > 0.0f && f < 100.0f) ||
                 !ports_set_dc_enabled(c->ports, port - 1, f > 0.0f))
            reply_err("DC failed");
        else
            reply_ok();
    } else if (strcmp(verb, "USB") == 0) {
        // USB <1-6> ON|OFF
        bool on = a2 && strcmp(a2, "ON") == 0;
        bool off = a2 && strcmp(a2, "OFF") == 0;
        if (!parse_int(a1, &port) || !(on || off))
            reply_err("usage: USB <1-6> ON|OFF");
        else if (!ports_set_usb(c->ports, port - 1, on))
            reply_err("USB failed");
        else
            reply_ok();
    } else if (strcmp(verb, "PWM") == 0) {
        // PWM <0.1-10>: DC output PWM frequency in Hz
        if (!parse_float(a1, &f))
            reply_err("usage: PWM <0.1-10>");
        else if (!ports_set_dc_pwm_hz(c->ports, f))
            reply_err("PWM failed");
        else
            printf("{\"ok\":true,\"dc_pwm_hz\":%.2f}\n", c->ports->dc_pwm_hz);
    } else if (strcmp(verb, "LED") == 0) {
        // LED <0-100>
        if (!parse_int(a1, &val))
            reply_err("usage: LED <0-100>");
        else if (!ports_set_led_brightness(c->ports, val))
            reply_err("LED failed");
        else
            reply_ok();
    } else if (strcmp(verb, "BOOTSEL") == 0) {
        reply_ok();
        stdio_flush();
        sleep_ms(50);
        reset_usb_boot(0, 0);
    } else if (strcmp(verb, "REBOOT") == 0) {
        reply_ok();
        stdio_flush();
        watchdog_reboot(0, 0, 50);
        for (;;)
            tight_loop_contents();
    } else {
        reply_err("unknown command");
    }
}

void cmd_poll(cmd_ctx_t *c) {
    static char line[CMD_LINE_MAX];
    static size_t len;
    static bool overflow;

    for (;;) {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT)
            return;
        if (ch == '\r' || ch == '\n') {
            if (overflow)
                reply_err("line too long");
            else if (len > 0) {
                line[len] = '\0';
                run(c, line);
            }
            len = 0;
            overflow = false;
        } else if (len < CMD_LINE_MAX - 1) {
            line[len++] = (char)ch;
        } else {
            overflow = true;
        }
    }
}
