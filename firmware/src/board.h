// Pin and I2C address map for the scope_hub board
#pragma once

#include <stdint.h>

#define BOARD_TABLE static const __attribute__((unused))

// I2C0: power monitoring and the PCA9685
#define I2C0_SDA_PIN 4
#define I2C0_SCL_PIN 5
#define I2C0_BAUD 400000

// I2C1: environment sensors, GPS DDC, Qwiic J10 and display header J9
#define I2C1_SDA_PIN 6
#define I2C1_SCL_PIN 7
#define I2C1_BAUD 100000

// GPS U58 NEO-M9N on UART1, 38400 8N1 by default
#define GPS_TX_PIN 20
#define GPS_RX_PIN 21
#define GPS_PPS_PIN 22
#define GPS_RESET_N_PIN 23
#define GPS_BAUD 38400

// Display header J9 (SPI0 plus I2C1)
#define DISP_GPIO15_PIN 15
#define DISP_MISO_PIN 16
#define DISP_CS_PIN 17
#define DISP_SCLK_PIN 18
#define DISP_MOSI_PIN 19

// Q25 drain with a 10k pull-up
#define LOCAL_5V_GOOD_PIN 2

// USB-C downstream ports
// ATTACH_N: low when a device is plugged in
// INHIBIT: high forces the port's VBUS switch off
#define USB_PORTS 6
BOARD_TABLE uint8_t usb_attach_n_pin[USB_PORTS] = {14, 13, 12, 11, 10, 9};
BOARD_TABLE uint8_t usb_inhibit_pin[USB_PORTS] = {29, 28, 27, 26, 25, 24};

// INA226 7-bit addresses and shunt resistances.
#define INA_IN_ADDR 0x46
#define INA_IN_SHUNT_OHM 0.001f

#define DC_PORTS 6
BOARD_TABLE uint8_t dc_ina_addr[DC_PORTS] = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45};
BOARD_TABLE float dc_shunt_ohm[DC_PORTS] = {0.010f, 0.020f, 0.020f, 0.020f, 0.020f, 0.020f};

BOARD_TABLE uint8_t usb_ina_addr[USB_PORTS] = {0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F};
BOARD_TABLE float usb_shunt_ohm[USB_PORTS] = {0.010f, 0.010f, 0.010f, 0.010f, 0.010f, 0.005f};

// PCA9685 U57 (A5 high, A4..A0 low)
#define PCA9685_ADDR 0x60
// DC port n: channel 2n drives the TPS1H100 IN (high = on)
// channel 2n+1 sinks the port LED (low = lit)
#define PCA_DC_EN_CH(n) (2 * (n))
#define PCA_DC_LED_CH(n) (2 * (n) + 1)
#define PCA_USBLED_12_CH 12
#define PCA_USBLED_34_CH 13
#define PCA_USBLED_56_CH 14
#define PCA_USBLED_UP_CH 15

// Environment sensors on I2C1.
#define SHT45_ADDR 0x44
#define VEML7700_ADDR 0x10
#define BMP581_ADDR 0x46
#define BMP581_ADDR_ALT 0x47
#define NEO_M9N_ADDR 0x42
