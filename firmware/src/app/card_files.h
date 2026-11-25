#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CardFilesReport {
    bool library_created;
    bool card_full;
    bool headphones_created;
} CardFilesReport;

typedef struct CardStartScan {
    bool checked : 1;
    bool covers_pending : 1;
    bool rescan : 1;
    bool check_music : 1;
    uint8_t rescan_stops;
} CardStartScan;

size_t card_files_count(void);
const char *card_files_path(size_t index);
bool card_files_slow(size_t index);
bool card_files_missing(size_t index);
void card_files_reserve(CardFilesReport *report, size_t index);
bool card_files_prepare_step(size_t *prepare_index, CardFilesReport *report, bool charging,
                             bool *finished);
CardStartScan card_files_start_scan(bool created, bool available, bool rebuilt);
