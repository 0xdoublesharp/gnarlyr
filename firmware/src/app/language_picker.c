#include "language_picker.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "notice_band.h"
#include "ui/header.h"
#include "ui/list_navigation.h"

enum {
    KNOWN_LANGUAGES = 11,
    PRIMARY_BYTES = 2
};

static const char CHECK_MARK[] = "\xE2\x9C\x93";
static const char KNOWN_CODES[KNOWN_LANGUAGES][PRIMARY_BYTES] = {
    {'e', 'n'}, {'d', 'e'}, {'f', 'r'}, {'e', 's'}, {'i', 't'}, {'p', 't'},
    {'n', 'l'}, {'r', 'u'}, {'j', 'a'}, {'k', 'o'}, {'z', 'h'}};
static const StringId EXONYMS[KNOWN_LANGUAGES] = {
    STR_LANGUAGE_ENGLISH,  STR_LANGUAGE_GERMAN,     STR_LANGUAGE_FRENCH, STR_LANGUAGE_SPANISH,
    STR_LANGUAGE_ITALIAN,  STR_LANGUAGE_PORTUGUESE, STR_LANGUAGE_DUTCH,  STR_LANGUAGE_RUSSIAN,
    STR_LANGUAGE_JAPANESE, STR_LANGUAGE_KOREAN,     STR_LANGUAGE_CHINESE};

static char lower(char letter)
{
    return letter >= 'A' && letter <= 'Z' ? (char)(letter - 'A' + 'a') : letter;
}

const char *language_exonym(const char *code)
{
    bool primary = code[0] != '\0' && code[1] != '\0' && (code[2] == '\0' || code[2] == '-');
    for (int index = 0; primary && index < KNOWN_LANGUAGES; index++) {
        if (lower(code[0]) == KNOWN_CODES[index][0] && lower(code[1]) == KNOWN_CODES[index][1]) {
            return i18n_text(EXONYMS[index]);
        }
    }
    return NULL;
}

static bool same_text(const char *first, const char *second)
{
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

static bool needs_fonts(const LanguagePicker *picker, int index)
{
    return language_list_needs_fonts(picker->config.languages, index);
}

static int language_count(const void *context)
{
    const LanguagePicker *picker = context;
    return picker->config.languages->count;
}

static const char *language_label(const void *context, int index)
{
    const LanguagePicker *picker = context;
    return picker->labels[index];
}

static bool is_current(const LanguagePicker *picker, int index)
{
    return language_list_find(picker->config.languages, i18n_code()) == index;
}

static const char *language_detail(const void *context, int index)
{
    const LanguagePicker *picker = context;
    const char *code = picker->config.languages->codes[index];
    const char *exonym = language_exonym(code);
    if (is_current(picker, index)) {
        return CHECK_MARK;
    }
    if (needs_fonts(picker, index)) {
        return i18n_text(STR_NEEDS_FONTS);
    }
    bool same = exonym != NULL && same_text(exonym, picker->config.languages->names[index]);
    return same ? NULL : exonym;
}

static ListRowRole language_role(const void *context, int index)
{
    const LanguagePicker *picker = context;
    return needs_fonts(picker, index) && !is_current(picker, index) ? LIST_ROW_NEEDS
                                                                    : LIST_ROW_PLAIN;
}

static const char *drawable_name(const LanguagePicker *picker, int index)
{
    return language_exonym(picker->config.languages->codes[index]);
}

static void build_label(LanguagePicker *picker, int index)
{
    const LanguageList *languages = picker->config.languages;
    StringBuilder label = string_builder_make(picker->labels[index], LANGUAGE_LABEL_BYTES);
    const char *exonym = drawable_name(picker, index);
    if (!needs_fonts(picker, index)) {
        string_builder_append(&label, languages->names[index]);
    } else if (exonym != NULL) {
        const char *const parts[] = {exonym, languages->codes[index]};
        i18n_format(&label, STR_LANGUAGE_FALLBACK, parts, 2);
    } else {
        string_builder_append(&label, languages->codes[index]);
    }
}

const char *language_picker_notice(const LanguagePicker *picker, char *text, uint32_t capacity)
{
    int index = picker->explained;
    if (index >= 0 && index == picker->list.selected) {
        const char *exonym = drawable_name(picker, index);
        StringBuilder notice = string_builder_make(text, capacity);
        i18n_format_text(&notice, STR_LANGUAGE_NEEDS_FONTS,
                         exonym != NULL ? exonym : picker->config.languages->codes[index]);
        return text;
    }
    if (picker->config.languages->count <= 1) {
        return i18n_text(STR_LANGUAGE_ADD_PACKS);
    }
    return NULL;
}

static ScreenAction choose_language(void *context, int index)
{
    LanguagePicker *picker = context;
    if (needs_fonts(picker, index) && !is_current(picker, index)) {
        picker->explained = index;
        return screen_action_none();
    }
    picker->config.choose(picker->config.context, index);
    return screen_action_pop();
}

static Rect list_area(const LanguagePicker *picker, const char *notice)
{
    const Theme *theme = picker->config.theme;
    Rect content = theme_content_area(theme);
    if (notice != NULL) {
        content.height -= notice_band_height(theme, notice, content.width);
    }
    return content;
}

static void fit(LanguagePicker *picker)
{
    char text[LANGUAGE_NOTICE_BYTES];
    Rect area = list_area(picker, language_picker_notice(picker, text, sizeof text));
    list_view_fit_rows(&picker->list, list_view_rows_in(&picker->list, area.height));
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    LanguagePicker *picker = context;
    ListSelection selection = {.select = choose_language, .hold = NULL, .context = picker};
    ScreenAction action = list_navigation_handle(&picker->list, event, now_ms, selection);
    if (picker->explained != picker->list.selected) {
        picker->explained = -1;
    }
    fit(picker);
    return action;
}

static void enter(void *context, uint32_t now_ms)
{
    LanguagePicker *picker = context;
    picker->now_ms = now_ms;
    picker->explained = -1;
    for (int index = 0; index < picker->config.languages->count; index++) {
        build_label(picker, index);
    }
    list_view_measure(&picker->list, theme_content_area(picker->config.theme).width);
    fit(picker);
    int current = language_list_find(picker->config.languages, i18n_code());
    list_view_select(&picker->list, current < 0 ? 0 : current, now_ms);
}

static void update(void *context, uint32_t now_ms)
{
    LanguagePicker *picker = context;
    picker->now_ms = now_ms;
}

static void draw(void *context, Canvas *canvas)
{
    const LanguagePicker *picker = context;
    const Theme *theme = picker->config.theme;
    header_draw(canvas, theme, picker->screen.title);
    char text[LANGUAGE_NOTICE_BYTES];
    const char *notice = language_picker_notice(picker, text, sizeof text);
    Rect area = list_area(picker, notice);
    list_view_draw(&picker->list, canvas, area, picker->now_ms);
    Rect content = theme_content_area(theme);
    if (notice != NULL) {
        Rect band = {content.x, area.y + area.height, content.width, content.height - area.height};
        notice_band_draw(canvas, theme, band, notice);
    }
}

Screen *language_picker_init(LanguagePicker *picker, LanguagePickerConfig config)
{
    picker->config = config;
    picker->explained = -1;
    picker->model = (ListModel){.context = picker,
                                .count = language_count,
                                .label = language_label,
                                .detail = language_detail,
                                .role = language_role,
                                .wraps = true};
    list_view_init(&picker->list, &picker->model, config.theme, 0);
    picker->screen = (Screen){
        .title = i18n_text(STR_LANGUAGE),
        .context = picker,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &picker->screen;
}
