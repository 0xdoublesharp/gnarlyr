#pragma once

enum {
    TEXT_SPY_CAPACITY = 1024,
    TEXT_SPY_TEXT = 224
};

typedef struct TextSpyEntry {
    char shown[TEXT_SPY_TEXT];
    char rest[TEXT_SPY_TEXT];
} TextSpyEntry;

void text_spy_reset(void);
int text_spy_count(void);
const TextSpyEntry *text_spy_entry(int index);
