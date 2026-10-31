#include "env.h"

#include "board.h"
#include "i2cbus.h"
#include "pico/time.h"

#define ENV_BUS i2c1
#define ENV_PERIOD_US 1000000
#define SHT45_WAIT_US 10000 // high-precision measurement takes up to 8.3 ms

// SHT45
#define SHT45_MEASURE_HIGH 0xFD
#define SHT45_SOFT_RESET 0x94

// BMP581
#define BMP_REG_CHIP_ID 0x01
#define BMP_REG_TEMP_XLSB 0x1D // 0x1D..0x1F temperature, 0x20..0x22 pressure
#define BMP_REG_OSR_CONFIG 0x36
#define BMP_REG_ODR_CONFIG 0x37
#define BMP_REG_CMD 0x7E
#define BMP_CHIP_ID 0x50
#define BMP_CMD_SOFT_RESET 0xB6
// press_en=1, osr_p=16x (4), osr_t=2x (1)
#define BMP_OSR_VALUE 0x61
// deep_dis=1, odr=1 Hz (0x1C), pwr_mode=normal (1)
#define BMP_ODR_VALUE (0x80 | (0x1C << 2) | 0x01)

// VEML7700
#define ALS_REG_CONF 0x00
#define ALS_REG_PSM 0x03
#define ALS_REG_DATA 0x04
#define ALS_SATURATION 60000
#define ALS_RANGE_DOWN 20000 // step to a less sensitive range above this count
#define ALS_RANGE_UP 200     // step to a more sensitive range below this count

// Ranges from most to least sensitive. Gain sits in bits 12:11 (01 x2, 00 x1,
// 11 x1/4, 10 x1/8) and integration time in bits 9:6 (0011 800 ms, 0010 400,
// 0001 200, 0000 100, 1000 50, 1100 25). Resolution is the datasheet's
// 0.0042 lux per count at x2 and 800 ms, scaled by gain and time.
typedef struct {
    uint16_t conf;
    float lux_per_count;
} als_range_t;

static const als_range_t als_ranges[] = {
    {(1u << 11) | (0x3u << 6), 0.0042f}, {(1u << 11) | (0x2u << 6), 0.0084f},
    {(1u << 11) | (0x1u << 6), 0.0168f}, {(1u << 11) | (0x0u << 6), 0.0336f},
    {(0u << 11) | (0x0u << 6), 0.0672f}, {(3u << 11) | (0x0u << 6), 0.2688f},
    {(2u << 11) | (0x0u << 6), 0.5376f}, {(2u << 11) | (0x8u << 6), 1.0752f},
    {(2u << 11) | (0xCu << 6), 2.1504f},
};
#define ALS_RANGES (int)(sizeof als_ranges / sizeof als_ranges[0])
#define ALS_FIRST_LOW_GAIN 5 // ranges from here use gain x1/4 or x1/8

static uint8_t sht_crc(const uint8_t *d) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

static bool sht_start(void) {
    uint8_t cmd = SHT45_MEASURE_HIGH;
    return i2c_write_bytes(ENV_BUS, SHT45_ADDR, &cmd, 1);
}

static void sht_finish(env_t *env) {
    uint8_t d[6];
    env->sht_valid = false;
    if (!i2c_read_bytes(ENV_BUS, SHT45_ADDR, d, sizeof d))
        return;
    if (sht_crc(&d[0]) != d[2] || sht_crc(&d[3]) != d[5])
        return;
    uint16_t t = (uint16_t)((d[0] << 8) | d[1]);
    uint16_t rh = (uint16_t)((d[3] << 8) | d[4]);
    env->temp_c = -45.0f + 175.0f * (float)t / 65535.0f;
    float h = -6.0f + 125.0f * (float)rh / 65535.0f;
    env->rh_pct = h < 0.0f ? 0.0f : (h > 100.0f ? 100.0f : h);
    env->sht_valid = true;
}

// The address follows the SDO level: 0x46 low, 0x47 high. The schematic ties SDO
// low, but the first board answers at 0x47, so both are tried.
static uint8_t bmp_addr;

static bool bmp_try(uint8_t addr) {
    uint8_t id;
    if (!i2c_write_reg8(ENV_BUS, addr, BMP_REG_CMD, BMP_CMD_SOFT_RESET))
        return false;
    sleep_ms(3);
    if (!i2c_read_reg(ENV_BUS, addr, BMP_REG_CHIP_ID, &id, 1) || id != BMP_CHIP_ID)
        return false;
    bmp_addr = addr;
    return i2c_write_reg8(ENV_BUS, addr, BMP_REG_OSR_CONFIG, BMP_OSR_VALUE) &&
           i2c_write_reg8(ENV_BUS, addr, BMP_REG_ODR_CONFIG, BMP_ODR_VALUE);
}

static bool bmp_init(void) { return bmp_try(BMP581_ADDR) || bmp_try(BMP581_ADDR_ALT); }

static void bmp_read(env_t *env) {
    uint8_t d[6];
    env->bmp_valid = false;
    if (!i2c_read_reg(ENV_BUS, bmp_addr, BMP_REG_TEMP_XLSB, d, sizeof d))
        return;
    int32_t t = (int32_t)(((uint32_t)d[2] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[0] << 8)) >> 8;
    uint32_t p = ((uint32_t)d[5] << 16) | ((uint32_t)d[4] << 8) | d[3];
    env->baro_temp_c = (float)t / 65536.0f;
    env->pressure_hpa = (float)p / 64.0f / 100.0f;
    env->bmp_valid = p != 0;
}

static bool als_set_range(env_t *env, int r) {
    env->als_range = (uint8_t)r;
    env->als_settling = true; // the data register holds the old range's count until one integration ends
    return i2c_write_reg16_le(ENV_BUS, VEML7700_ADDR, ALS_REG_CONF, als_ranges[r].conf);
}

static bool als_init(env_t *env) {
    return als_set_range(env, 0) && i2c_write_reg16_le(ENV_BUS, VEML7700_ADDR, ALS_REG_PSM, 0);
}

// Runs once per second. The longest integration is 800 ms, so one skipped
// reading after a range change is enough.
static void als_read(env_t *env) {
    uint16_t raw;
    if (env->als_settling) {
        env->als_settling = false;
        return;
    }
    if (!i2c_read_reg16_le(ENV_BUS, VEML7700_ADDR, ALS_REG_DATA, &raw)) {
        env->als_valid = false;
        return;
    }
    int r = env->als_range;
    env->als_raw = raw;
    env->als_saturated = raw >= ALS_SATURATION && r == ALS_RANGES - 1;
    float lux = (float)raw * als_ranges[r].lux_per_count;
    if (r >= ALS_FIRST_LOW_GAIN) {
        // Vishay's correction for the low-gain ranges (application note 84323).
        float x = lux;
        lux = ((6.0135e-13f * x - 9.3924e-9f) * x + 8.1488e-5f) * x * x + 1.0023f * x;
    }
    env->lux = lux;
    env->als_valid = true;

    if (raw > ALS_RANGE_DOWN && r < ALS_RANGES - 1)
        als_set_range(env, r + 1);
    else if (raw < ALS_RANGE_UP && r > 0)
        als_set_range(env, r - 1);
}

void env_init(env_t *env) {
    *env = (env_t){0};
    // The SHT45 NACKs a read header when no measurement is pending, so a read
    // probe misses it. A soft-reset command ACKs instead.
    uint8_t reset = SHT45_SOFT_RESET;
    env->sht_present = i2c_write_bytes(ENV_BUS, SHT45_ADDR, &reset, 1);
    sleep_ms(1);
    env->bmp_present = bmp_init();
    env->als_present = als_init(env);
}

void env_poll(env_t *env, uint64_t now_us) {
    static uint64_t next_cycle_us;
    static uint64_t sht_ready_us;
    static bool sht_pending;

    if (sht_pending && now_us >= sht_ready_us) {
        sht_pending = false;
        sht_finish(env);
    }
    if (now_us < next_cycle_us)
        return;
    next_cycle_us = now_us + ENV_PERIOD_US;

    // One sensor per call, so no single pass of the main loop holds off USB
    // for more than one I2C transaction at 100 kHz.
    static int step;
    if (step == 0) {
        if (env->sht_present && sht_start()) {
            sht_pending = true;
            sht_ready_us = now_us + SHT45_WAIT_US;
        } else {
            env->sht_valid = false;
        }
    } else if (step == 1) {
        if (env->bmp_present)
            bmp_read(env);
    } else {
        if (env->als_present)
            als_read(env);
    }
    step = (step + 1) % 3;
    next_cycle_us = step ? now_us : now_us + ENV_PERIOD_US;
}
