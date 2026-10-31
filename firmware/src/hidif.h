// HID feature-report interface to the host. See hid_proto.h for the layouts.
#pragma once

#include "dew.h"
#include "env.h"
#include "gps.h"
#include "ledauto.h"
#include "ports.h"

void hidif_init(ports_t *ports, env_t *env, gps_t *gps, dew_t *dew, ledauto_t *led, bool watchdog_reset);

void hidif_poll(uint64_t now_us);
