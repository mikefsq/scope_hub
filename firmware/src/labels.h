// User names for the DC and USB ports.
//
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LABEL_LEN 16
#define LABEL_PORTS 6

typedef struct __attribute__((packed)) {
    char dc[LABEL_PORTS][LABEL_LEN];
    char usb[LABEL_PORTS][LABEL_LEN];
} labels_t;

void labels_init(void);
const labels_t *labels_get(void);

void labels_set(const labels_t *l);

void labels_poll(void);
