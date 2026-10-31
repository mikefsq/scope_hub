#include "pca9685.h"

#include "i2cbus.h"
#include "pico/time.h"

#define REG_MODE1 0x00
#define REG_MODE2 0x01
#define REG_LED0_ON_L 0x06
#define REG_PRE_SCALE 0xFE
#define OSC_HZ 25000000.0f

#define MODE1_RESTART 0x80
#define MODE1_AI 0x20
#define MODE1_SLEEP 0x10
#define MODE2_OUTDRV 0x04 // totem-pole outputs
#define FULL_BIT 0x10     // bit 4 of LEDn_ON_H / LEDn_OFF_H

bool pca9685_read_mode1(i2c_inst_t *bus, uint8_t addr, uint8_t *mode1) {
    return i2c_read_reg(bus, addr, REG_MODE1, mode1, 1);
}

bool pca9685_channel_on(i2c_inst_t *bus, uint8_t addr, uint8_t ch, bool *on) {
    uint8_t r[4];
    if (!i2c_read_reg(bus, addr, (uint8_t)(REG_LED0_ON_L + 4 * ch), r, 4))
        return false;
    uint16_t on_c = (uint16_t)(r[0] | ((r[1] & 0x0F) << 8));
    uint16_t off_c = (uint16_t)(r[2] | ((r[3] & 0x0F) << 8));
    if (r[3] & FULL_BIT)
        *on = false; // full off takes priority
    else if (r[1] & FULL_BIT)
        *on = true;
    else
        *on = on_c != off_c;
    return true;
}

bool pca9685_init(i2c_inst_t *bus, uint8_t addr) {
    if (!i2c_write_reg8(bus, addr, REG_MODE1, MODE1_AI))
        return false;
    sleep_us(500); // oscillator start-up after leaving sleep
    return i2c_write_reg8(bus, addr, REG_MODE2, MODE2_OUTDRV);
}

bool pca9685_set_freq(i2c_inst_t *bus, uint8_t addr, float hz, float *actual_hz) {
    uint8_t mode1;
    int prescale = (int)(OSC_HZ / (4096.0f * hz) + 0.5f) - 1;
    if (prescale < 3)
        prescale = 3;
    if (prescale > 255)
        prescale = 255;
    uint8_t current;
    if (!i2c_read_reg(bus, addr, REG_MODE1, &mode1, 1) || !i2c_read_reg(bus, addr, REG_PRE_SCALE, &current, 1))
        return false;
    if (current == prescale && !(mode1 & MODE1_SLEEP)) {
        // Already running at this frequency: no sleep cycle, no output glitch.
        *actual_hz = OSC_HZ / (4096.0f * (float)(prescale + 1));
        return true;
    }
    mode1 &= (uint8_t)~MODE1_RESTART;
    // PRE_SCALE only accepts writes while the oscillator sleeps. The PWM registers
    // keep their contents, and RESTART resumes every channel afterwards.
    if (!i2c_write_reg8(bus, addr, REG_MODE1, mode1 | MODE1_SLEEP) ||
        !i2c_write_reg8(bus, addr, REG_PRE_SCALE, (uint8_t)prescale) ||
        !i2c_write_reg8(bus, addr, REG_MODE1, mode1 & (uint8_t)~MODE1_SLEEP))
        return false;
    sleep_us(500);
    if (!i2c_write_reg8(bus, addr, REG_MODE1, (mode1 & (uint8_t)~MODE1_SLEEP) | MODE1_RESTART))
        return false;
    *actual_hz = OSC_HZ / (4096.0f * (float)(prescale + 1));
    return true;
}

bool pca9685_set(i2c_inst_t *bus, uint8_t addr, uint8_t ch, uint16_t on_count, uint16_t phase) {
    uint8_t b[5];
    b[0] = (uint8_t)(REG_LED0_ON_L + 4 * ch);
    if (on_count == 0) {
        b[1] = 0;
        b[2] = 0;
        b[3] = 0;
        b[4] = FULL_BIT; // full off wins over everything
    } else if (on_count >= 4096) {
        b[1] = 0;
        b[2] = FULL_BIT; // full on
        b[3] = 0;
        b[4] = 0;
    } else {
        uint16_t on = phase & 0x0FFF;
        uint16_t off = (uint16_t)((on + on_count) & 0x0FFF);
        b[1] = (uint8_t)on;
        b[2] = (uint8_t)(on >> 8);
        b[3] = (uint8_t)off;
        b[4] = (uint8_t)(off >> 8);
    }
    return i2c_write_bytes(bus, addr, b, sizeof b);
}
