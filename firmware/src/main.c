// scope_hub controller firmware for the RP2350A
#include "cmd.h"
#include "dew.h"
#include "env.h"
#include "gps.h"
#include "hardware/watchdog.h"
#include "hidif.h"
#include "i2cbus.h"
#include "labels.h"
#include "ledauto.h"
#include "pico/stdlib.h"
#include "ports.h"
#include "settings.h"
#include "tusb.h"

// The main loop feeds the watchdog on every pass.
#define WATCHDOG_MS 2000

static ports_t ports;
static env_t env;
static gps_t gps;
static dew_t dew;
static ledauto_t led;

int main(void) {
    bool watchdog_reset = watchdog_enable_caused_reboot();
    stdio_init_all(); // UART0 on J19: text commands for debugging

    i2cbus_init();
    ports_init(&ports);
    env_init(&env);
    gps_init();
    dew_init(&dew);
    ledauto_init(&led);
    labels_init();

    settings_ctx_t sc = {.ports = &ports, .dew = &dew, .led = &led};
    settings_t saved;
    if (settings_load(&saved))
        settings_apply(&sc, &saved);

    hidif_init(&ports, &env, &gps, &dew, &led, watchdog_reset);
    tud_init(BOARD_TUD_RHPORT);
    watchdog_enable(WATCHDOG_MS, true);

    cmd_ctx_t ctx = {.ports = &ports, .env = &env, .gps = &gps};
    for (;;) {
        uint64_t now = time_us_64();
        watchdog_update();
        tud_task();
        gps_poll(&gps);
        ports_poll(&ports, now);
        env_poll(&env, now);
        dew_poll(&dew, &env, &ports, now);
        ledauto_poll(&led, &env, &ports, now);
        cmd_poll(&ctx);
        hidif_poll(now);
        labels_poll();
    }
}
