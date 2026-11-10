#include "text_spy.h"

#include <stdio.h>
#include <string.h>

#define text_draw text_spy_real_draw
#define text_draw_span text_spy_real_draw_span
#include "text.c"
#undef text_draw
#undef text_draw_span

int text_draw(Canvas *canvas, TextStyle style, Point pen, const char *text);
int text_draw_span(Canvas *canvas, TextStyle style, Point pen, TextSpan span);

static TextSpyEntry entries[TEXT_SPY_CAPACITY];
static int entry_count;

void text_spy_reset(void)
{
    entry_count = 0;
}

int text_spy_count(void)
{
    return entry_count;
}

const TextSpyEntry *text_spy_entry(int index)
{
    return &entries[index];
}

static void record(TextSpan span)
{
    TextSpyEntry entry = {0};
    size_t shown = span.byte_count < TEXT_SPY_TEXT - 1 ? span.byte_count : TEXT_SPY_TEXT - 1;
    memcpy(entry.shown, span.start, shown);
    snprintf(entry.rest, sizeof entry.rest, "%s", span.start);
    for (int index = 0; index < entry_count; index++) {
        if (memcmp(&entries[index], &entry, sizeof entry) == 0) {
            return;
        }
    }
    if (entry_count < TEXT_SPY_CAPACITY) {
        entries[entry_count++] = entry;
    }
}

int text_draw(Canvas *canvas, TextStyle style, Point pen, const char *text)
{
    record((TextSpan){text, strlen(text)});
    return text_spy_real_draw(canvas, style, pen, text);
}

int text_draw_span(Canvas *canvas, TextStyle style, Point pen, TextSpan span)
{
    record(span);
    return text_spy_real_draw_span(canvas, style, pen, span);
}
