#include "key_test_screen.h"

#include "app/app.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "gfx/font.h"
#include "hal/input.h"
#include "ui/header.h"
#include "ui/label.h"

enum {
    KEY_TEST_TEXT_CAPACITY = 48,
    KEY_TEST_NUMBER_CAPACITY = 12
};

_Static_assert(STR_KEY_POWER - STR_KEY_VOLUME_UP == KEY_POWER - KEY_VOLUME_UP,
               "key names follow the keys");
_Static_assert(STR_KEY_REPEAT - STR_KEY_TAP == KEY_GESTURE_REPEAT - KEY_GESTURE_PRESS,
               "gesture names follow the gestures");

Key key_test_band(uint16_t reading)
{
    Key key = KEY_POWER;
    return hal_input_decode_key(reading, &key) ? key : (Key)KEY_COUNT;
}

static const char *key_name(Key key)
{
    return i18n_text((int)key < KEY_COUNT ? (StringId)((int)STR_KEY_VOLUME_UP + (int)key)
                                          : STR_ABOUT_NONE);
}

static void remember(KeyTestScreen *test, uint16_t reading, uint32_t now_ms)
{
    if (test->count > 0 && key_test_band(test->history[0].reading) == key_test_band(reading)) {
        return;
    }
    for (int index = KEY_TEST_HISTORY - 1; index > 0; index--) {
        test->history[index] = test->history[index - 1];
    }
    test->history[0] = (KeyTestEntry){.time_ms = now_ms, .reading = reading};
    test->count = (uint8_t)(test->count < KEY_TEST_HISTORY ? test->count + 1 : KEY_TEST_HISTORY);
}

static void update(void *context, uint32_t now_ms)
{
    KeyTestScreen *test = context;
    test->reading = hal_input_key_adc();
    remember(test, test->reading, now_ms);
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    KeyTestScreen *test = context;
    (void)now_ms;
    app_wake();
    if (event.key == KEY_POWER) {
        return screen_action_pop();
    }
    test->last = event;
    test->has_last = true;
    return screen_action_none();
}

static void draw_line(Canvas *canvas, TextStyle style, Rect *area, const char *text)
{
    int height = font_line_height(style.font);
    label_draw(canvas, style, (Label){(Rect){area->x, area->y, area->width, height}, text});
    area->y += height;
    area->height -= height;
}

static void draw_reading(Canvas *canvas, const Theme *theme, Rect *area, uint16_t reading)
{
    char digits[KEY_TEST_NUMBER_CAPACITY];
    StringBuilder builder = string_builder_make(digits, sizeof digits);
    string_builder_append_unsigned(&builder, reading);
    TextStyle style = {theme->fonts.title, theme->colors.text, true};
    int cell = text_digit_width(style.font) * KEY_TEST_SCALE;
    int height = font_line_height(style.font) * KEY_TEST_SCALE;
    Rect box = {area->x, area->y, cell, height};
    for (const char *digit = digits; *digit != '\0'; digit++) {
        text_draw_glyph_scaled(canvas, style, box, (uint32_t)*digit, KEY_TEST_SCALE);
        box.x += cell;
    }
    TextStyle band = {theme->fonts.body, theme->colors.accent, false};
    label_draw(canvas, band,
               (Label){(Rect){box.x + theme->metrics.padding, area->y,
                              area->width - (box.x - area->x) - theme->metrics.padding, height},
                       key_name(key_test_band(reading))});
    area->y += height;
    area->height -= height;
}

static void draw_event(Canvas *canvas, const KeyTestScreen *test, const Theme *theme, Rect *area)
{
    char text[KEY_TEST_TEXT_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    if (test->has_last) {
        const char *parts[] = {key_name(test->last.key),
                               i18n_text((StringId)((int)STR_KEY_TAP + (int)test->last.gesture))};
        i18n_format(&builder, test->last.chord ? STR_KEY_TEST_CHORD : STR_KEY_TEST_EVENT, parts, 2);
    }
    const Font *font =
        text_width(theme->fonts.body, text) <= area->width ? theme->fonts.body : theme->fonts.small;
    draw_line(canvas, (TextStyle){font, theme->colors.text, false}, area, text);
}

static void draw_history(Canvas *canvas, const KeyTestScreen *test, const Theme *theme, Rect area)
{
    TextStyle style = {theme->fonts.small, theme->colors.text_secondary, true};
    int rows = area.height / font_line_height(style.font);
    for (int index = 0; index < test->count && index < rows; index++) {
        const KeyTestEntry *entry = &test->history[index];
        char time[KEY_TEST_NUMBER_CAPACITY];
        char reading[KEY_TEST_NUMBER_CAPACITY];
        char text[KEY_TEST_TEXT_CAPACITY];
        StringBuilder number = string_builder_make(time, sizeof time);
        string_builder_append_unsigned(&number, entry->time_ms);
        number = string_builder_make(reading, sizeof reading);
        string_builder_append_unsigned(&number, entry->reading);
        const char *parts[] = {time, reading, key_name(key_test_band(entry->reading))};
        StringBuilder builder = string_builder_make(text, sizeof text);
        i18n_format(&builder, STR_KEY_TEST_ENTRY, parts, 3);
        draw_line(canvas, style, &area, text);
    }
}

static Rect draw_footer(Canvas *canvas, const Theme *theme, Rect area)
{
    TextStyle style = {theme->fonts.small, theme->colors.text_tertiary, false};
    const char *caution = i18n_text(STR_KEY_TEST_CAUTION);
    int line = font_line_height(style.font);
    int height = line * (1 + label_wrap_count(style.font, caution, area.width));
    Rect footer = {area.x, area.y + area.height - height, area.width, height};
    draw_line(canvas, (TextStyle){style.font, theme->colors.text, false}, &footer,
              i18n_text(STR_KEY_TEST_HINT));
    label_draw_paragraph(canvas, style, (Label){footer, caution});
    return (Rect){area.x, area.y, area.width, area.height - height};
}

static void draw(void *context, Canvas *canvas)
{
    const KeyTestScreen *test = context;
    const Theme *theme = test->theme;
    Rect content = theme_content_area(theme);
    int padding = theme->metrics.padding;
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, test->screen.title);
    Rect area = {content.x + padding, content.y + padding, content.width - 2 * padding,
                 content.height - 2 * padding};
    area = draw_footer(canvas, theme, area);
    bool wide = !theme_is_portrait(theme);
    Rect column = {area.x, area.y, wide ? area.width / 2 : area.width, area.height};
    draw_reading(canvas, theme, &column, test->reading);
    draw_event(canvas, test, theme, &column);
    Rect history = {area.x + area.width / 2 + padding, area.y, area.width / 2 - padding,
                    area.height};
    draw_history(canvas, test, theme, wide ? history : column);
}

static void enter(void *context, uint32_t now_ms)
{
    KeyTestScreen *test = context;
    test->has_last = false;
    test->count = 0;
    update(test, now_ms);
}

void key_test_screen_init(KeyTestScreen *test, const Theme *theme)
{
    *test = (KeyTestScreen){.theme = theme};
    test->screen = (Screen){.title = i18n_text(STR_ABOUT_KEYS),
                            .context = test,
                            .keys = SCREEN_KEYS_TEST,
                            .enter = enter,
                            .handle_key = handle_key,
                            .update = update,
                            .draw = draw};
}
