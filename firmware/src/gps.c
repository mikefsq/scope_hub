#include "gps.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "pico/time.h"

#define GPS_UART uart1
#define RX_BUF_SIZE 1024 // power of two
#define NMEA_LINE_MAX 100
#define FIELDS_MAX 20

static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head, rx_tail;
static volatile uint32_t pps_count;
static volatile uint64_t pps_last_us;

// PPS edges from the ISR, read in gps_poll.
#define EDGE_RING 8
#define STAT_WINDOW 32
static volatile uint64_t edge_us[EDGE_RING];
static volatile uint32_t edge_head;
static uint32_t edge_tail;
static uint64_t prev_edge_us;
static int32_t intervals[STAT_WINDOW]; // interval minus 1 s, in us
static int n_intervals, next_interval;
static uint64_t reset_release_us; // nonzero while RESET_N is held low

#define RESET_PULSE_US 100000

static void uart_rx_isr(void) {
    while (uart_is_readable(GPS_UART)) {
        uint8_t c = (uint8_t)uart_get_hw(GPS_UART)->dr;
        uint32_t next = (rx_head + 1) & (RX_BUF_SIZE - 1);
        if (next != rx_tail) { // drop on overflow
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
}

static void pps_isr(uint gpio, uint32_t events) {
    if (gpio == GPS_PPS_PIN && (events & GPIO_IRQ_EDGE_RISE)) {
        uint64_t t = time_us_64();
        pps_last_us = t;
        pps_count++;
        edge_us[edge_head % EDGE_RING] = t;
        edge_head++;
    }
}

void gps_init(void) {
    gpio_init(GPS_RESET_N_PIN);
    gpio_put(GPS_RESET_N_PIN, 1); // RESET_N is active low
    gpio_set_dir(GPS_RESET_N_PIN, GPIO_OUT);

    uart_init(GPS_UART, GPS_BAUD);
    gpio_set_function(GPS_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(GPS_RX_PIN, GPIO_FUNC_UART);
    uart_set_fifo_enabled(GPS_UART, true);
    irq_set_exclusive_handler(UART1_IRQ, uart_rx_isr);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irq_enables(GPS_UART, true, false);

    gpio_init(GPS_PPS_PIN);
    gpio_set_dir(GPS_PPS_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(GPS_PPS_PIN, GPIO_IRQ_EDGE_RISE, true, pps_isr);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

// Verifies "$...*hh" and cuts the line at '*'.
static bool checksum_ok(char *line) {
    if (line[0] != '$')
        return false;
    uint8_t sum = 0;
    char *p = line + 1;
    while (*p && *p != '*')
        sum ^= (uint8_t)*p++;
    if (*p != '*')
        return false;
    int h = hexval(p[1]), l = hexval(p[2]);
    if (h < 0 || l < 0)
        return false;
    *p = '\0';
    return sum == (uint8_t)(h * 16 + l);
}

// Splits on commas in place, keeping empty fields.
static int split(char *s, char **f) {
    int n = 0;
    f[n++] = s;
    for (; *s && n < FIELDS_MAX; s++) {
        if (*s == ',') {
            *s = '\0';
            f[n++] = s + 1;
        }
    }
    return n;
}

// NMEA ddmm.mmmm / dddmm.mmmm with hemisphere to signed degrees.
static bool to_degrees(const char *v, const char *hemi, double *out) {
    if (!*v || !*hemi)
        return false;
    double raw = atof(v);
    int deg = (int)(raw / 100.0);
    double d = deg + (raw - deg * 100.0) / 60.0;
    if (*hemi == 'S' || *hemi == 'W')
        d = -d;
    *out = d;
    return true;
}

// GSV state per talker (GP, GL, GA, GB, GQ, GN). A sequence of GSV sentences
// is summed into the pending counts, then committed by its last sentence.
#define TALKERS 6
#define GSV_FRESH_US 3000000
static const char talker_ids[TALKERS][3] = {"GP", "GL", "GA", "GB", "GQ", "GN"};
typedef struct {
    uint8_t in_view, tracked, max_cnr;
    uint8_t p_tracked, p_max_cnr;
    uint64_t at_us;
} gsv_t;
static gsv_t gsv[TALKERS];

static int talker_index(const char *id) {
    for (int i = 0; i < TALKERS; i++)
        if (id[0] == talker_ids[i][0] && id[1] == talker_ids[i][1])
            return i;
    return -1;
}

static void parse_gsv(gps_t *gps, char **f, int n, const char *talker) {
    int t = talker_index(talker);
    if (t < 0 || n < 4)
        return;
    gsv_t *g = &gsv[t];
    int total = atoi(f[1]), num = atoi(f[2]);
    if (num == 1) {
        g->p_tracked = 0;
        g->p_max_cnr = 0;
    }
    // Satellite blocks of four fields start at field 4. A trailing signal ID
    // makes the field count one more than a multiple of four.
    for (int i = 4; i + 3 < n; i += 4) {
        if (f[i + 3][0]) {
            int cnr = atoi(f[i + 3]);
            g->p_tracked++;
            if (cnr > g->p_max_cnr)
                g->p_max_cnr = (uint8_t)cnr;
        }
    }
    if (num == total) {
        g->in_view = (uint8_t)atoi(f[3]);
        g->tracked = g->p_tracked;
        g->max_cnr = g->p_max_cnr;
        g->at_us = time_us_64();
    }
    uint8_t view = 0, tracked = 0, cnr = 0;
    uint64_t now = time_us_64();
    for (int i = 0; i < TALKERS; i++) {
        if (gsv[i].at_us && now - gsv[i].at_us < GSV_FRESH_US) {
            view = (uint8_t)(view + gsv[i].in_view);
            tracked = (uint8_t)(tracked + gsv[i].tracked);
            if (gsv[i].max_cnr > cnr)
                cnr = gsv[i].max_cnr;
        }
    }
    gps->sats_in_view = view;
    gps->sats_tracked = tracked;
    gps->max_cnr = cnr;
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

// Labels the latest PPS edge with the UTC second of an RMC sentence. The
// receiver sends the sentence for an epoch shortly after that epoch's pulse.
static void label_pps(gps_t *gps, const char *hms, const char *dmy) {
    if (strlen(hms) < 6 || strlen(dmy) != 6)
        return;
    const char *frac = strchr(hms, '.');
    if (frac && atoi(frac + 1) != 0)
        return; // only whole-second epochs line up with a pulse
    uint64_t edge = pps_last_us;
    if (!edge || time_us_64() - edge > 950000)
        return;
    int hh = (hms[0] - '0') * 10 + hms[1] - '0', mi = (hms[2] - '0') * 10 + hms[3] - '0';
    int ss = (hms[4] - '0') * 10 + hms[5] - '0';
    int dd = (dmy[0] - '0') * 10 + dmy[1] - '0', mo = (dmy[2] - '0') * 10 + dmy[3] - '0';
    int yy = 2000 + (dmy[4] - '0') * 10 + dmy[5] - '0';
    gps->pps_utc_s = days_from_civil(yy, mo, dd) * 86400 + hh * 3600 + mi * 60 + ss;
    gps->pps_utc_dev_us = edge;
    gps->pps_utc_valid = true;
}

// Folds new PPS edges into the interval statistics. An interval more than
// 1 ms off 1 s breaks the run and restarts the statistics.
static void pps_stats(gps_t *gps) {
    while (edge_tail != edge_head) {
        uint64_t t = edge_us[edge_tail % EDGE_RING];
        edge_tail++;
        if (prev_edge_us) {
            int64_t d = (int64_t)(t - prev_edge_us) - 1000000;
            if (d > -1000 && d < 1000) {
                intervals[next_interval] = (int32_t)d;
                next_interval = (next_interval + 1) % STAT_WINDOW;
                if (n_intervals < STAT_WINDOW)
                    n_intervals++;
            } else {
                n_intervals = 0;
                next_interval = 0;
            }
        }
        prev_edge_us = t;
    }
    gps->pps_intervals = (uint16_t)n_intervals;
    if (n_intervals == 0)
        return;
    float sum = 0, sq = 0;
    for (int i = 0; i < n_intervals; i++)
        sum += (float)intervals[i];
    float mean = sum / (float)n_intervals;
    for (int i = 0; i < n_intervals; i++)
        sq += ((float)intervals[i] - mean) * ((float)intervals[i] - mean);
    gps->crystal_ppm = mean; // us per second is ppm
    gps->pps_jitter_us = n_intervals > 1 ? sqrtf(sq / (float)(n_intervals - 1)) : 0.0f;
}

static void copy_field(char *dst, size_t size, const char *src) {
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static void parse_line(gps_t *gps, char *line) {
    char *f[FIELDS_MAX];
    if (!checksum_ok(line))
        return;
    int n = split(line + 1, f);
    size_t len = strlen(f[0]);
    if (len < 3)
        return;
    const char *type = f[0] + len - 3; // skip the talker ID (GP, GN, ...)
    gps->seen = true;
    gps->last_sentence_us = time_us_64();

    if (strcmp(type, "GGA") == 0 && n >= 10) {
        copy_field(gps->utc, sizeof gps->utc, f[1]);
        gps->fix_quality = atoi(f[6]);
        gps->satellites = atoi(f[7]);
        gps->hdop = (float)atof(f[8]);
        if (gps->fix_quality > 0) {
            to_degrees(f[2], f[3], &gps->lat_deg);
            to_degrees(f[4], f[5], &gps->lon_deg);
            gps->alt_m = (float)atof(f[9]);
        }
    } else if (strcmp(type, "GSV") == 0) {
        parse_gsv(gps, f, n, f[0]);
    } else if (strcmp(type, "RMC") == 0 && n >= 10) {
        copy_field(gps->utc, sizeof gps->utc, f[1]);
        gps->rmc_valid = f[2][0] == 'A';
        copy_field(gps->date, sizeof gps->date, f[9]);
        if (gps->rmc_valid) {
            label_pps(gps, f[1], f[9]);
            to_degrees(f[3], f[4], &gps->lat_deg);
            to_degrees(f[5], f[6], &gps->lon_deg);
        }
    }
}

static void gps_forget(gps_t *gps) {
    *gps = (gps_t){0};
    memset(gsv, 0, sizeof gsv);
    uint32_t irq = save_and_disable_interrupts();
    pps_count = 0;
    pps_last_us = 0;
    edge_tail = edge_head;
    restore_interrupts(irq);
    prev_edge_us = 0;
    n_intervals = 0;
    next_interval = 0;
}

void gps_reset_pin(gps_t *gps) {
    gpio_put(GPS_RESET_N_PIN, 0);
    reset_release_us = time_us_64() + RESET_PULSE_US;
    gps_forget(gps);
}

// UBX-CFG-RST (class 0x06, id 0x04): navBbrMask 0xFFFF clears every battery-backed
// item, resetMode 0x02 restarts only the GNSS engine, so the UART stays up.
void gps_cold_start(gps_t *gps) {
    uint8_t msg[] = {0xB5, 0x62, 0x06, 0x04, 0x04, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0, 0};
    uint8_t a = 0, b = 0;
    for (size_t i = 2; i < sizeof msg - 2; i++) {
        a = (uint8_t)(a + msg[i]);
        b = (uint8_t)(b + a);
    }
    msg[sizeof msg - 2] = a;
    msg[sizeof msg - 1] = b;
    uart_write_blocking(GPS_UART, msg, sizeof msg);
    gps_forget(gps);
}

void gps_poll(gps_t *gps) {
    static char line[NMEA_LINE_MAX];
    static size_t len;

    if (reset_release_us && time_us_64() >= reset_release_us) {
        gpio_put(GPS_RESET_N_PIN, 1);
        reset_release_us = 0;
    }

    while (rx_tail != rx_head) {
        char c = (char)rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & (RX_BUF_SIZE - 1);
        if (c == '$')
            len = 0;
        if (c == '\r' || c == '\n') {
            if (len > 0) {
                line[len] = '\0';
                parse_line(gps, line);
                len = 0;
            }
        } else if (len < NMEA_LINE_MAX - 1) {
            line[len++] = c;
        } else {
            len = 0; // overlong line, wait for the next '$'
        }
    }

    uint32_t irq = save_and_disable_interrupts();
    gps->pps_count = pps_count;
    gps->pps_last_us = pps_last_us;
    restore_interrupts(irq);
    pps_stats(gps);
}
