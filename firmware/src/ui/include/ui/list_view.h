#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/icons.h"
#include "ui/theme.h"

enum {
    LIST_FAST_SCROLL_AFTER_MS = 400,
    LIST_SECTION_JUMP_AFTER_MS = 600,
    LIST_FAST_SCROLL_ROWS = 4,
    LIST_FASTEST_SCROLL_ROWS = 12,
    LIST_SECTION_SHOWN_MS = 600,
    LIST_REPEAT_DELAY_MS = 240,
    LIST_REPEAT_INTERVAL_MS = 80,
    LIST_SECTION_BEFORE_LETTERS = 0,
    LIST_SECTION_FIRST_LETTER = 1,
    LIST_SECTION_AFTER_LETTERS = 27
};

typedef struct ListSections {
    int first;
    int end;
} ListSections;

typedef struct ListModel {
    const void *context;
    int (*count)(const void *context);
    const char *(*label)(const void *context, int index);
    const char *(*detail)(const void *context, int index);
    bool (*opens)(const void *context, int index);
    int detail_max_width;
    int (*section)(const void *context, int index);
    IconId (*icon)(const void *context, int index);
    bool detail_below;
    ListSections (*sections)(const void *context, int index);
    bool section_reverse;
} ListModel;

typedef struct ListView {
    const ListModel *model;
    const Theme *theme;
    int visible_rows;
    int selected;
    int top;
    uint32_t selection_changed_ms;
    uint32_t scroll_started_ms;
    uint32_t section_shown_until_ms;
    uint8_t background;
} ListView;

void list_view_init(ListView *view, const ListModel *model, const Theme *theme, uint32_t now_ms);
void list_view_move(ListView *view, int delta, bool wrap, uint32_t now_ms);
void list_view_start_scroll(ListView *view, int direction, uint32_t now_ms);
void list_view_keep_scrolling(ListView *view, int direction, uint32_t now_ms);
int list_section_of_key(const char *key);
const char *list_section_label(int section);
void list_view_fit(ListView *view);
void list_view_fit_rows(ListView *view, int rows);
void list_view_restart_marquee(ListView *view, uint32_t now_ms);
void list_view_select(ListView *view, int index, uint32_t now_ms);
void list_view_draw(const ListView *view, Canvas *canvas, Rect area, uint32_t now_ms);

int list_view_row_height(const ListView *view);
int list_view_rows_in(const ListView *view, int height);
