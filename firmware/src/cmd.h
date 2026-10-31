// Line-based command protocol on stdio (USB CDC and UART0).
#pragma once

#include "env.h"
#include "gps.h"
#include "ports.h"

typedef struct {
    ports_t *ports;
    env_t *env;
    gps_t *gps;
} cmd_ctx_t;

void cmd_poll(cmd_ctx_t *ctx);
