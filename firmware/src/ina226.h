// TI INA226 current and bus-voltage monitor.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"

typedef struct {
    uint8_t addr;
    float shunt_ohm;
    uint16_t config; // CONFIG register value; 0 selects the default below
    bool present; // answered with the TI manufacturer ID at init
    bool valid;   // the last read succeeded
    float volts;  // bus voltage
    float amps;   // shunt voltage divided by the shunt resistance
} ina226_t;

// Checks the manufacturer ID and sets 16-sample averaging, 1.1 ms conversions,
// continuous shunt and bus measurement.
bool ina226_init(i2c_inst_t *bus, ina226_t *dev);
bool ina226_read(i2c_inst_t *bus, ina226_t *dev);
