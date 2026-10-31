// u-blox NEO-M9N on UART1: NMEA GGA/RMC parsing and PPS edge capture.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool seen;        // any valid NMEA sentence received
    int fix_quality;  // GGA field 6: 0 none, 1 GPS, 2 DGPS, ...
    int satellites;   // GGA field 7
    bool rmc_valid;   // RMC status 'A'
    double lat_deg;   // + north
    double lon_deg;   // + east
    float alt_m;      // GGA altitude above mean sea level
    char utc[11];     // hhmmss.ss
    char date[7];     // ddmmyy
    float hdop;              // GGA field 8
    uint8_t sats_in_view;    // GSV, summed over constellations seen in the last 3 s
    uint8_t sats_tracked;    // of those, satellites with a signal-to-noise value
    uint8_t max_cnr;         // strongest carrier-to-noise ratio, dB-Hz
    uint64_t last_sentence_us;
    uint32_t pps_count;
    uint64_t pps_last_us;

    // PPS timing. Edges are stamped with the 1 us system timer, which runs
    // from the 12 MHz crystal.
    bool pps_utc_valid;      // an edge carries a UTC label from RMC
    int64_t pps_utc_s;       // Unix seconds at that edge
    uint64_t pps_utc_dev_us; // system timer at that edge
    uint16_t pps_intervals;  // consecutive good 1 s intervals in the statistics, up to 32
    float crystal_ppm;       // mean interval error: positive when the crystal runs fast
    float pps_jitter_us;     // standard deviation of the intervals
} gps_t;

void gps_init(void);

void gps_poll(gps_t *gps);

void gps_reset_pin(gps_t *gps);

void gps_cold_start(gps_t *gps);
