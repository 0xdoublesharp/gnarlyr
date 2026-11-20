#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    RECORD_MAGIC_BYTES = 4,
    RECORD_HEADER_BYTES = 6,
    RECORD_SEAL_BYTES = 4
};

typedef struct RecordKind {
    uint8_t magic[RECORD_MAGIC_BYTES];
    uint16_t version;
} RecordKind;

void record_begin(uint8_t *bytes, const RecordKind *kind);
bool record_is(const uint8_t *bytes, const RecordKind *kind);
void record_seal(uint8_t *bytes, uint32_t length);
bool record_sealed(const uint8_t *bytes, uint32_t length);
