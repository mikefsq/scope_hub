#include "labels.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

#define LABELS_MAGIC 0x4C425348u // "HSBL"
#define LABELS_VERSION 1
#define LABELS_OFFSET (PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    labels_t labels;
    uint32_t crc;
} labels_record_t;

#define RECORD_PAGES ((sizeof(labels_record_t) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE)

static labels_t current;
static bool write_pending;

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));
    }
    return ~c;
}

static const labels_record_t *stored(void) { return (const labels_record_t *)(XIP_BASE + LABELS_OFFSET); }

static bool valid(const labels_record_t *r) {
    return r->magic == LABELS_MAGIC && r->version == LABELS_VERSION && r->size == sizeof *r &&
           r->crc == crc32((const uint8_t *)r, offsetof(labels_record_t, crc));
}

// Forces NUL termination so a name never runs into the next one.
static void terminate(labels_t *l) {
    for (int i = 0; i < LABEL_PORTS; i++) {
        l->dc[i][LABEL_LEN - 1] = '\0';
        l->usb[i][LABEL_LEN - 1] = '\0';
    }
}

void labels_init(void) {
    memset(&current, 0, sizeof current);
    if (valid(stored()))
        memcpy(&current, &stored()->labels, sizeof current);
    terminate(&current);
}

const labels_t *labels_get(void) { return &current; }

void labels_set(const labels_t *l) {
    memcpy(&current, l, sizeof current);
    terminate(&current);
    write_pending = true;
}

void labels_poll(void) {
    if (!write_pending)
        return;
    write_pending = false;
    if (valid(stored()) && memcmp(&stored()->labels, &current, sizeof current) == 0)
        return; // unchanged: no erase

    static uint8_t buf[RECORD_PAGES * FLASH_PAGE_SIZE];
    labels_record_t r = {.magic = LABELS_MAGIC, .version = LABELS_VERSION, .size = sizeof r, .labels = current};
    r.crc = crc32((const uint8_t *)&r, offsetof(labels_record_t, crc));
    memset(buf, 0xFF, sizeof buf);
    memcpy(buf, &r, sizeof r);

    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(LABELS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(LABELS_OFFSET, buf, sizeof buf);
    restore_interrupts(irq);
}
