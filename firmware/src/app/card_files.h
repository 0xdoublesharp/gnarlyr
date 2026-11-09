#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct CardFilesReport {
    bool library_created;
    bool card_full;
} CardFilesReport;

size_t card_files_count(void);
const char *card_files_path(size_t index);
bool card_files_slow(size_t index);
bool card_files_missing(size_t index);
void card_files_reserve(CardFilesReport *report, size_t index);
