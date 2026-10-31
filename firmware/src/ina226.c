#include "ina226.h"

#include "i2cbus.h"

#define REG_CONFIG 0x00
#define REG_SHUNT 0x01
#define REG_BUS 0x02
#define REG_MFG_ID 0xFE

#define MFG_ID_TI 0x5449

// Bit 14 is reserved and reads 1. AVG=16 (010), VBUSCT=1.1 ms (100),
// VSHCT=1.1 ms (100), MODE=shunt and bus continuous (111).
#define CONFIG_VALUE 0x4527

#define SHUNT_LSB_V 2.5e-6f
#define BUS_LSB_V 1.25e-3f

bool ina226_init(i2c_inst_t *bus, ina226_t *dev) {
    uint16_t id;
    dev->present = i2c_read_reg16_be(bus, dev->addr, REG_MFG_ID, &id) && id == MFG_ID_TI;
    dev->valid = false;
    if (!dev->present)
        return false;
    return i2c_write_reg16_be(bus, dev->addr, REG_CONFIG, dev->config ? dev->config : CONFIG_VALUE);
}

bool ina226_read(i2c_inst_t *bus, ina226_t *dev) {
    uint16_t shunt, vbus;
    if (!dev->present) {
        dev->valid = false;
        return false;
    }
    if (!i2c_read_reg16_be(bus, dev->addr, REG_SHUNT, &shunt) ||
        !i2c_read_reg16_be(bus, dev->addr, REG_BUS, &vbus)) {
        dev->valid = false;
        return false;
    }
    dev->amps = (float)(int16_t)shunt * SHUNT_LSB_V / dev->shunt_ohm;
    dev->volts = (float)vbus * BUS_LSB_V;
    dev->valid = true;
    return true;
}
