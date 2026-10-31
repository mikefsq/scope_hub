#include "ports.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pca9685.h"
#include "tusb.h"

#define POWER_BUS i2c0
#define POLL_PERIOD_US 250000
#define PWM_STEPS 4096

// The PCA9685 for LED dimming. Its prescaler is shared by all 16 channels
#define LED_PWM_HZ 1000.0f
#define LED_DEFAULT_PCT 10

// DC PWM: 100 steps per period
#define DC_PWM_DEFAULT_HZ 1.0f
#define DC_PWM_MIN_HZ 0.1f
#define DC_PWM_MAX_HZ 10.0f

// The DC current monitors
#define DC_INA_CONFIG 0x4327 // AVG 4, VBUSCT and VSHCT 1.1 ms, continuous
#define DC_SETTLE_US 20000

// the output-high time is the part of each PWM period the LED is dark.
static bool led_set(ports_t *p, uint8_t ch, bool lit) {
    if (!p->pca_present)
        return false;
    uint16_t dark = PWM_STEPS;
    if (lit)
        dark = (uint16_t)(PWM_STEPS - (p->led_pct * PWM_STEPS + 50) / 100);
    return pca9685_set(POWER_BUS, PCA9685_ADDR, ch, dark, 0);
}

static void leds_refresh(ports_t *p) {
    for (int i = 0; i < DC_PORTS; i++)
        led_set(p, PCA_DC_LED_CH(i), ports_dc_effective(p, i) > 0);
    led_set(p, PCA_USBLED_12_CH, p->usb_attached[0] || p->usb_attached[1]);
    led_set(p, PCA_USBLED_34_CH, p->usb_attached[2] || p->usb_attached[3]);
    led_set(p, PCA_USBLED_56_CH, p->usb_attached[4] || p->usb_attached[5]);
    led_set(p, PCA_USBLED_UP_CH, p->host_mounted);
}

static bool dc_write(ports_t *p, int port, bool on, uint64_t now_us) {
    if (!pca9685_set(POWER_BUS, PCA9685_ADDR, PCA_DC_EN_CH(port), on ? PWM_STEPS : 0, 0))
        return false;
    if (on != p->dc_on[port])
        p->dc_edge_us[port] = now_us;
    p->dc_on[port] = on;
    return true;
}

// Each output's period is offset by a sixth so the six turn-on edges spread out.
static void dc_pwm_run(ports_t *p, uint64_t now_us) {
    uint64_t period = (uint64_t)(1e6f / p->dc_pwm_hz);
    for (int i = 0; i < DC_PORTS; i++) {
        uint8_t duty = ports_dc_effective(p, i);
        if (duty == 0 || duty == 100)
            continue; // static levels are written once by dc_apply
        uint64_t phase = (now_us + period * (uint64_t)i / DC_PORTS) % period;
        bool want = phase < period * duty / 100;
        if (want != p->dc_on[i]) {
            dc_write(p, i, want, now_us);
            // Schedule one reading inside this on-phase when it lasts long enough.
            uint64_t on_us = period * duty / 100;
            if (want && on_us > DC_SETTLE_US + 5000)
                p->dc_read_at_us[i] = now_us + DC_SETTLE_US;
        }
    }
}

void ports_init(ports_t *p) {
    *p = (ports_t){0};
    p->led_pct = LED_DEFAULT_PCT;
    p->dc_pwm_hz = DC_PWM_DEFAULT_HZ;

    for (int i = 0; i < USB_PORTS; i++) {
        gpio_init(usb_attach_n_pin[i]);
        gpio_set_dir(usb_attach_n_pin[i], GPIO_IN); // 10k external pull-ups
        gpio_init(usb_inhibit_pin[i]);
        gpio_put(usb_inhibit_pin[i], 0); // 0 lets the HD3SS3220 attach logic drive VBUS
        gpio_set_dir(usb_inhibit_pin[i], GPIO_OUT);
    }
    gpio_init(LOCAL_5V_GOOD_PIN);
    gpio_set_dir(LOCAL_5V_GOOD_PIN, GPIO_IN);

    p->input = (ina226_t){.addr = INA_IN_ADDR, .shunt_ohm = INA_IN_SHUNT_OHM};
    ina226_init(POWER_BUS, &p->input);
    for (int i = 0; i < DC_PORTS; i++) {
        p->dc_mon[i] = (ina226_t){.addr = dc_ina_addr[i], .shunt_ohm = dc_shunt_ohm[i], .config = DC_INA_CONFIG};
        ina226_init(POWER_BUS, &p->dc_mon[i]);
    }
    for (int i = 0; i < USB_PORTS; i++) {
        p->usb_mon[i] = (ina226_t){.addr = usb_ina_addr[i], .shunt_ohm = usb_shunt_ohm[i]};
        ina226_init(POWER_BUS, &p->usb_mon[i]);
    }

    // Power-on leaves every PCA9685 output fully low: DC outputs off, all LEDs lit.
    // An MCU-only reset leaves the chip running with the old outputs.
    uint8_t mode1;
    p->pca_retained = pca9685_read_mode1(POWER_BUS, PCA9685_ADDR, &mode1) && !(mode1 & 0x10);
    bool retained_on[DC_PORTS] = {false};
    if (p->pca_retained) {
        for (int i = 0; i < DC_PORTS; i++)
            pca9685_channel_on(POWER_BUS, PCA9685_ADDR, PCA_DC_EN_CH(i), &retained_on[i]);
    }
    p->pca_present = pca9685_init(POWER_BUS, PCA9685_ADDR) &&
                     pca9685_set_freq(POWER_BUS, PCA9685_ADDR, LED_PWM_HZ, &p->led_pwm_hz);
    for (int i = 0; i < DC_PORTS; i++) {
        p->dc_duty[i] = 100;
        p->dc_enabled[i] = retained_on[i];
        ports_set_dc(p, i, 100);
    }
    leds_refresh(p);
}

void ports_poll(ports_t *p, uint64_t now_us) {
    static uint64_t next_us;

    if (p->pca_present)
        dc_pwm_run(p, now_us);

    // A reading scheduled inside a PWM on-phase comes first.
    for (int i = 0; i < DC_PORTS; i++) {
        if (p->dc_read_at_us[i] && now_us >= p->dc_read_at_us[i]) {
            p->dc_read_at_us[i] = 0;
            ina226_t *m = &p->dc_mon[i];
            if (p->dc_on[i] && ina226_read(POWER_BUS, m)) {
                p->dc_volts_on[i] = m->volts;
                p->dc_amps_on[i] = m->amps;
                p->dc_on_valid[i] = true;
            }
            return;
        }
    }

    // one of the 13 monitors are read per call
    static int step = -1; // -1 idle, 0 input, 1..6 DC, 7..12 USB
    if (step >= 0) {
        if (step == 0) {
            ina226_read(POWER_BUS, &p->input);
        } else if (step <= DC_PORTS) {
            int i = step - 1;
            ina226_t *m = &p->dc_mon[i];
            if (ina226_read(POWER_BUS, m) && p->dc_on[i] && now_us - p->dc_edge_us[i] >= DC_SETTLE_US) {
                p->dc_volts_on[i] = m->volts;
                p->dc_amps_on[i] = m->amps;
                p->dc_on_valid[i] = true;
            }
        } else {
            ina226_read(POWER_BUS, &p->usb_mon[step - 1 - DC_PORTS]);
        }
        step = step + 1 > DC_PORTS + USB_PORTS ? -1 : step + 1;
        return;
    }

    if (now_us < next_us)
        return;
    next_us = now_us + POLL_PERIOD_US;
    step = 0;

    bool changed = false;
    for (int i = 0; i < USB_PORTS; i++) {
        bool attached = !gpio_get(usb_attach_n_pin[i]);
        changed |= attached != p->usb_attached[i];
        p->usb_attached[i] = attached;
    }
    bool mounted = tud_mounted();
    changed |= mounted != p->host_mounted;
    p->host_mounted = mounted;
    p->local_5v_pin = gpio_get(LOCAL_5V_GOOD_PIN);

    if (changed)
        leds_refresh(p);
}

// Drives an output from its switch and duty cycle.
static bool dc_apply(ports_t *p, int port) {
    uint64_t now = time_us_64();
    uint8_t duty = ports_dc_effective(p, port);
    if (duty == 0 || duty == 100) {
        if (!dc_write(p, port, duty == 100, now))
            return false;
    } else {
        dc_pwm_run(p, now); // start the new duty right away
    }
    return led_set(p, PCA_DC_LED_CH(port), duty > 0);
}

bool ports_set_dc(ports_t *p, int port, int duty_pct) {
    if (port < 0 || port >= DC_PORTS || duty_pct < 0 || duty_pct > 100 || !p->pca_present)
        return false;
    p->dc_duty[port] = (uint8_t)duty_pct;
    return dc_apply(p, port);
}

bool ports_set_dc_pwm(ports_t *p, int port, bool on) {
    if (port < 0 || port >= DC_PORTS || !p->pca_present)
        return false;
    p->dc_pwm[port] = on;
    return dc_apply(p, port);
}

bool ports_set_dc_enabled(ports_t *p, int port, bool on) {
    if (port < 0 || port >= DC_PORTS || !p->pca_present)
        return false;
    p->dc_enabled[port] = on;
    return dc_apply(p, port);
}

bool ports_set_dc_pwm_hz(ports_t *p, float hz) {
    if (!(hz >= DC_PWM_MIN_HZ && hz <= DC_PWM_MAX_HZ))
        return false;
    p->dc_pwm_hz = hz;
    return true;
}

bool ports_dc_avg_amps(const ports_t *p, int port, float *amps) {
    uint8_t duty = ports_dc_effective(p, port);
    if (duty == 0 || duty == 100) {
        *amps = p->dc_mon[port].amps;
        return p->dc_mon[port].valid;
    }
    *amps = p->dc_amps_on[port] * (float)duty / 100.0f;
    return p->dc_on_valid[port];
}

bool ports_set_usb(ports_t *p, int port, bool enabled) {
    if (port < 0 || port >= USB_PORTS)
        return false;
    p->usb_inhibit[port] = !enabled;
    gpio_put(usb_inhibit_pin[port], !enabled);
    return true;
}

bool ports_set_led_brightness(ports_t *p, int pct) {
    if (pct < 0 || pct > 100 || !p->pca_present)
        return false;
    p->led_pct = (uint8_t)pct;
    leds_refresh(p);
    return true;
}
