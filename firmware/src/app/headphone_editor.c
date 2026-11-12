#include "headphone_editor.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/clock.h"
#include "ui/header.h"
#include "ui/list_navigation.h"
#include "ui/navigation.h"

enum {
    ROOT,
    EDIT,
    VALUE
};
enum {
    ACTIVE,
    NAME,
    TRIM,
    BALANCE,
    CEILING,
    FIELD_COUNT
};

static const StringId FIELDS[] = {STR_HEADPHONE_USE, STR_HEADPHONE_NAME, STR_HEADPHONE_TRIM,
                                  STR_HEADPHONE_BALANCE, STR_HEADPHONE_CEILING};
static const StringId NAMES[] = {STR_HEADPHONE_IN_EAR, STR_HEADPHONE_OVER_EAR,
                                 STR_HEADPHONE_ON_EAR, STR_HEADPHONE_EARBUDS,
                                 STR_HEADPHONE_STUDIO, STR_HEADPHONE_TRAVEL};

static const HeadphoneProfile *profile_of(const HeadphoneEditor *editor)
{
    return &editor->config.store->profiles.slots[editor->slot];
}

static int count(const void *context)
{
    const HeadphoneEditor *editor = context;
    if (editor->mode != VALUE) {
        return FIELD_COUNT;
    }
    switch (editor->field) {
    case ACTIVE:
        return HEADPHONE_PROFILE_LIMIT + 1;
    case NAME:
        return (int)(sizeof NAMES / sizeof NAMES[0]);
    case TRIM:
        return HEADPHONE_PROFILE_TRIM_MAX_HALF_DB + 1;
    case BALANCE:
        return 2 * HEADPHONE_PROFILE_BALANCE_MAX + 1;
    case CEILING:
        return SETTINGS_VOLUME_MAX + 1;
    }
    return 0;
}

static const char *value_text(const HeadphoneEditor *editor, int field, int value, char *buffer)
{
    StringBuilder text = string_builder_make(buffer, sizeof editor->label);
    if (field == TRIM) {
        i18n_format_counts(&text, STR_HEADPHONE_TRIM_VALUE, (uint32_t)value / 2,
                           (uint32_t)(value % 2) * 5);
    } else if (field == BALANCE && value != 0) {
        i18n_format_counts(&text, value < 0 ? STR_HEADPHONE_LEFT : STR_HEADPHONE_RIGHT,
                           (uint32_t)(value < 0 ? -value : value), 0);
    } else if (field == BALANCE) {
        string_builder_append(&text, i18n_text(STR_HEADPHONE_CENTER));
    } else {
        string_builder_append_unsigned(&text, (uint32_t)value);
    }
    return buffer;
}

static const char *row_label(const void *context, int index)
{
    const HeadphoneEditor *editor = context;
    const HeadphoneProfiles *profiles = &editor->config.store->profiles;
    if (editor->mode == ROOT) {
        return index == 0 ? i18n_text(STR_HEADPHONE_ACTIVE) : profiles->slots[index - 1].name;
    }
    if (editor->mode == EDIT) {
        return i18n_text(FIELDS[index]);
    }
    if (editor->field == ACTIVE) {
        return index == 0 ? i18n_text(STR_OFF) : profiles->slots[index - 1].name;
    }
    if (editor->field == NAME) {
        return i18n_text(NAMES[index]);
    }
    return value_text(editor, editor->field,
                      editor->field == BALANCE ? index - HEADPHONE_PROFILE_BALANCE_MAX : index,
                      editor->label_text);
}

static bool same_text(const char *first, const char *second)
{
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

static int current(const HeadphoneEditor *editor)
{
    const HeadphoneProfile *profile = profile_of(editor);
    switch (editor->field) {
    case ACTIVE:
        return editor->config.store->profiles.current == HEADPHONE_PROFILE_NONE
                   ? 0
                   : editor->config.store->profiles.current + 1;
    case NAME:
        for (unsigned index = 0; index < sizeof NAMES / sizeof NAMES[0]; index++) {
            if (same_text(profile->name, i18n_text(NAMES[index]))) {
                return (int)index;
            }
        }
        return -1;
    case TRIM:
        return profile->trim_half_db;
    case BALANCE:
        return profile->balance + HEADPHONE_PROFILE_BALANCE_MAX;
    case CEILING:
        return profile->volume_ceiling;
    }
    return -1;
}

static const char *row_detail(const void *context, int index)
{
    const HeadphoneEditor *editor = context;
    const HeadphoneProfiles *profiles = &editor->config.store->profiles;
    if (editor->mode == VALUE) {
        return current(editor) == index ? "\xE2\x9C\x93" : NULL;
    }
    if (editor->mode == ROOT) {
        if (index == 0) {
            return profiles->current == HEADPHONE_PROFILE_NONE
                       ? i18n_text(STR_OFF)
                       : profiles->slots[profiles->current].name;
        }
        return profiles->current == index - 1 ? "\xE2\x9C\x93" : NULL;
    }
    const HeadphoneProfile *profile = profile_of(editor);
    if (index == ACTIVE) {
        return profiles->current == editor->slot ? "\xE2\x9C\x93" : NULL;
    }
    if (index == NAME) {
        return profile->name;
    }
    int value = index == TRIM      ? profile->trim_half_db
                : index == BALANCE ? profile->balance
                                   : profile->volume_ceiling;
    return value_text(editor, index, value, editor->detail_text);
}

static bool opens(const void *context, int index)
{
    const HeadphoneEditor *editor = context;
    return editor->mode == ROOT || (editor->mode == EDIT && index != ACTIVE);
}

static void change_view(HeadphoneEditor *editor, uint8_t mode, int selection)
{
    editor->mode = mode;
    editor->screen.title =
        mode == ROOT ? i18n_text(STR_HEADPHONES)
        : mode == EDIT
            ? profile_of(editor)->name
            : i18n_text(editor->field == ACTIVE ? STR_HEADPHONE_ACTIVE : FIELDS[editor->field]);
    editor->model.detail_below = mode != VALUE;
    list_view_init(&editor->list, &editor->model, editor->config.theme, editor->now_ms);
    list_view_select(&editor->list, selection < 0 ? 0 : selection, editor->now_ms);
}

static bool save(HeadphoneEditor *editor, const HeadphoneProfiles *candidate)
{
    uint8_t before = editor->config.player->volume;
    bool saved = headphone_store_commit(editor->config.store, candidate, editor->config.player,
                                        editor->config.settings);
    StringBuilder text = string_builder_make(editor->message, sizeof editor->message);
    if (saved && editor->config.player->volume < before) {
        i18n_format_counts(&text, STR_VOLUME_LOWERED, editor->config.player->volume, 0);
    } else {
        string_builder_append(&text,
                              i18n_text(saved ? STR_HEADPHONE_SAVED : STR_SETTINGS_NOT_SAVED));
    }
    toast_show(editor->config.toast, editor->message, hal_clock_milliseconds());
    return saved;
}

static void choose_value(HeadphoneEditor *editor, int index)
{
    HeadphoneProfiles candidate = editor->config.store->profiles;
    HeadphoneProfile *profile = &candidate.slots[editor->slot];
    switch (editor->field) {
    case ACTIVE:
        candidate.current = index == 0 ? HEADPHONE_PROFILE_NONE : (uint8_t)(index - 1);
        break;
    case NAME: {
        StringBuilder name = string_builder_make(profile->name, sizeof profile->name);
        string_builder_append(&name, i18n_text(NAMES[index]));
        break;
    }
    case TRIM:
        profile->trim_half_db = (uint8_t)index;
        break;
    case BALANCE:
        profile->balance = (int8_t)(index - HEADPHONE_PROFILE_BALANCE_MAX);
        break;
    case CEILING:
        profile->volume_ceiling = (uint8_t)index;
        break;
    }
    if (save(editor, &candidate)) {
        change_view(editor, editor->field == ACTIVE ? ROOT : EDIT, editor->field);
    }
}

static ScreenAction select_row(void *context, int index)
{
    HeadphoneEditor *editor = context;
    if (editor->mode == VALUE) {
        choose_value(editor, index);
    } else if (editor->mode == ROOT && index != 0) {
        editor->slot = (uint8_t)(index - 1);
        change_view(editor, EDIT, 0);
    } else if (editor->mode == EDIT && index == ACTIVE) {
        HeadphoneProfiles candidate = editor->config.store->profiles;
        candidate.current = editor->slot;
        save(editor, &candidate);
    } else {
        editor->field = (uint8_t)index;
        change_view(editor, VALUE, current(editor));
    }
    return screen_action_none();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    HeadphoneEditor *editor = context;
    editor->now_ms = now_ms;
    if (navigation_from_key(event).intent == NAVIGATION_BACK && editor->mode != ROOT) {
        bool root = editor->mode == EDIT || editor->field == ACTIVE;
        int selected = editor->mode == EDIT ? editor->slot + 1 : editor->field;
        change_view(editor, root ? ROOT : EDIT, selected);
        return screen_action_none();
    }
    ListSelection selection = {.select = select_row, .context = editor};
    return list_navigation_handle(&editor->list, event, now_ms, selection);
}

static void enter(void *context, uint32_t now_ms)
{
    HeadphoneEditor *editor = context;
    editor->now_ms = now_ms;
    list_view_fit(&editor->list);
}

static void update(void *context, uint32_t now_ms)
{
    ((HeadphoneEditor *)context)->now_ms = now_ms;
}

static void draw(void *context, Canvas *canvas)
{
    HeadphoneEditor *editor = context;
    const Theme *theme = editor->config.theme;
    header_draw(canvas, theme, editor->screen.title);
    list_view_draw(&editor->list, canvas, theme_content_area(theme), editor->now_ms);
}

Screen *headphone_editor_init(HeadphoneEditor *editor, HeadphoneEditorConfig config)
{
    *editor = (HeadphoneEditor){.config = config};
    headphone_store_fill_slots(&config.store->profiles);
    editor->label_text = editor->label;
    editor->detail_text = editor->detail;
    editor->model = (ListModel){.context = editor,
                                .count = count,
                                .label = row_label,
                                .detail = row_detail,
                                .opens = opens};
    editor->screen = (Screen){.context = editor,
                              .enter = enter,
                              .handle_key = handle_key,
                              .update = update,
                              .draw = draw,
                              .stays_on_wake = true};
    change_view(editor, ROOT, 0);
    return &editor->screen;
}
