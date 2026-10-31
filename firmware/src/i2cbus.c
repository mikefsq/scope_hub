#include "i2cbus.h"

#include "board.h"
#include "hardware/gpio.h"

// Per-transfer timeout.
#define I2C_TIMEOUT_US 5000

void i2cbus_init(void) {
    i2c_init(i2c0, I2C0_BAUD);
    gpio_set_function(I2C0_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C0_SCL_PIN, GPIO_FUNC_I2C);

    i2c_init(i2c1, I2C1_BAUD);
    gpio_set_function(I2C1_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C1_SCL_PIN, GPIO_FUNC_I2C);
}

bool i2c_write_bytes(i2c_inst_t *bus, uint8_t addr, const uint8_t *buf, size_t len) {
    return i2c_write_timeout_us(bus, addr, buf, len, false, I2C_TIMEOUT_US) == (int)len;
}

bool i2c_read_bytes(i2c_inst_t *bus, uint8_t addr, uint8_t *buf, size_t len) {
    return i2c_read_timeout_us(bus, addr, buf, len, false, I2C_TIMEOUT_US) == (int)len;
}

bool i2c_read_reg(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
    if (i2c_write_timeout_us(bus, addr, &reg, 1, true, I2C_TIMEOUT_US) != 1)
        return false;
    return i2c_read_timeout_us(bus, addr, buf, len, false, I2C_TIMEOUT_US) == (int)len;
}

bool i2c_write_reg8(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return i2c_write_bytes(bus, addr, b, 2);
}

bool i2c_write_reg16_be(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint16_t val) {
    uint8_t b[3] = {reg, (uint8_t)(val >> 8), (uint8_t)val};
    return i2c_write_bytes(bus, addr, b, 3);
}

bool i2c_read_reg16_be(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint16_t *val) {
    uint8_t b[2];
    if (!i2c_read_reg(bus, addr, reg, b, 2))
        return false;
    *val = (uint16_t)((b[0] << 8) | b[1]);
    return true;
}

bool i2c_write_reg16_le(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint16_t val) {
    uint8_t b[3] = {reg, (uint8_t)val, (uint8_t)(val >> 8)};
    return i2c_write_bytes(bus, addr, b, 3);
}

bool i2c_read_reg16_le(i2c_inst_t *bus, uint8_t addr, uint8_t reg, uint16_t *val) {
    uint8_t b[2];
    if (!i2c_read_reg(bus, addr, reg, b, 2))
        return false;
    *val = (uint16_t)(b[0] | (b[1] << 8));
    return true;
}

bool i2c_probe(i2c_inst_t *bus, uint8_t addr) {
    uint8_t b;
    return i2c_read_timeout_us(bus, addr, &b, 1, false, I2C_TIMEOUT_US) == 1;
}
