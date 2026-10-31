// Register-level I2C helpers with timeouts.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/i2c.h"

void i2cbus_init(void);

bool i2c_write_bytes(i2c_inst_t *bus, uint8_t addr, const uint8_t *buf, size_t len);
bool i2c_read_bytes(i2c_inst_t *bus, uint8_t addr, uint8_t *buf, size_t len);

bool i2c_read_reg(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
bool i2c_write_reg8(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint8_t val);
bool i2c_write_reg16_be(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint16_t val);
bool i2c_read_reg16_be(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint16_t *val);
bool i2c_write_reg16_le(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint16_t val);
bool i2c_read_reg16_le(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint16_t *val);

bool i2c_probe(i2c_inst_t *bus, uint8_t addr);
