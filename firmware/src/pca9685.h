// NXP PCA9685 16-channel PWM driver
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"

bool pca9685_read_mode1(i2c_inst_t *bus, uint8_t addr, uint8_t *mode1);
bool pca9685_channel_on(i2c_inst_t *bus, uint8_t addr, uint8_t ch, bool *on);
bool pca9685_init(i2c_inst_t *bus, uint8_t addr);
bool pca9685_set_freq(i2c_inst_t *bus, uint8_t addr, float hz, float *actual_hz);
bool pca9685_set(i2c_inst_t *bus, uint8_t addr, uint8_t ch, uint16_t on_count, uint16_t phase);
