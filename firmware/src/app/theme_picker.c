#include "theme_picker.h"

#include "app_screens.h"
#include "core/i18n.h"
#include "default_theme.h"
#include "ui/header.h"
#include "ui/list_navigation.h"

static const StringId BUILTINS[] = {STR_THEME_GRAPHITE, STR_THEME_NEON_NOIR, STR_THEME_SUNSET,
                                    STR_THEME_SEA_GLASS, STR_THEME_CLASSIC};

static int count(const void *context)
{
    (void)context;
    return APPEARANCE_THEME_COUNT + theme_picker_catalog()->count;
}

static const char *label(const void *context, int index)
{
    (void)context;
    return index < APPEARANCE_THEME_COUNT
               ? i18n_text(BUILTINS[index])
               : theme_picker_catalog()->entries[index - APPEARANCE_THEME_COUNT].name;
}

static const char *detail(const void *context, int index)
{
    (void)context;
    if (index < APPEARANCE_THEME_COUNT) {
        return NULL;
    }
    const ThemeCatalogEntry *entry =
        &theme_picker_catalog()->entries[index - APPEARANCE_THEME_COUNT];
    if (!entry->valid) {
        return i18n_text(STR_THEME_DAMAGED);
    }
    if (!entry->verified && !entry->low_contrast) {
        return entry->author;
    }
    char *text = theme_catalog_detail();
    StringBuilder builder = string_builder_make(text, 96);
    const char *arguments[] = {
        entry->author, i18n_text(entry->verified ? STR_THEME_VERIFIED : STR_THEME_LOW_CONTRAST)};
    i18n_format(&builder, STR_THEME_CREDIT, arguments, 2);
    return text;
}

static ThemeRuntime *runtime_of(ThemePicker *picker)
{
    return picker->screens->context.themes;
}

static void preview(ThemePicker *picker)
{
    if (picker->preview == picker->list.selected) {
        return;
    }
    picker->preview = picker->list.selected;
    ThemeRuntime *runtime = runtime_of(picker);
    int index = picker->preview;
    runtime->previewing = true;
    runtime->shown_base = runtime->saved_base;
    theme_runtime_close(runtime);
    if (index < APPEARANCE_THEME_COUNT) {
        runtime->shown_base = (uint8_t)index;
    } else {
        ThemeCatalogEntry *entry = &theme_picker_catalog()->entries[index - APPEARANCE_THEME_COUNT];
        if (entry->valid && !theme_runtime_load(runtime, entry->filename)) {
            entry->valid = false;
        }
    }
    default_theme_invalidate(picker->screens->context.styled_theme);
}

static ScreenAction select_theme(void *context, int index)
{
    ThemePicker *picker = context;
    ThemeRuntime *runtime = runtime_of(picker);
    if (index >= APPEARANCE_THEME_COUNT && !runtime->active) {
        toast_show(picker->screens->context.toast, i18n_text(STR_THEME_DAMAGED), picker->now_ms);
        return screen_action_none();
    }
    if (!theme_runtime_save(runtime, index < APPEARANCE_THEME_COUNT ? index : -1)) {
        toast_show(picker->screens->context.toast, i18n_text(STR_SETTINGS_NOT_SAVED),
                   picker->now_ms);
        return screen_action_none();
    }
    return screen_action_pop();
}

static ScreenAction key(void *context, KeyEvent event, uint32_t now_ms)
{
    ThemePicker *picker = context;
    picker->now_ms = now_ms;
    ListSelection selection = {.select = select_theme, .context = picker};
    ScreenAction action = list_navigation_handle(&picker->list, event, now_ms, selection);
    if (action.type == SCREEN_ACTION_NONE) {
        preview(picker);
    }
    return action;
}

static void enter(void *context, uint32_t now_ms)
{
    ThemePicker *picker = context;
    ThemeRuntime *runtime = runtime_of(picker);
    if (!runtime->stored) {
        runtime->saved_base = (uint8_t)picker->screens->context.settings->settings.appearance;
    }
    theme_catalog_scan(theme_picker_catalog());
    int selected = runtime->saved_base;
    for (unsigned at = 0; at < theme_picker_catalog()->count; at++) {
        if (theme_picker_catalog()->entries[at].valid &&
            theme_catalog_same_id(runtime->saved_id, theme_picker_catalog()->entries[at].id)) {
            selected = APPEARANCE_THEME_COUNT + (int)at;
        }
    }
    picker->now_ms = now_ms;
    picker->preview = -1;
    list_view_select(&picker->list, selected, now_ms);
    preview(picker);
}

static void update(void *context, uint32_t now_ms)
{
    ThemePicker *picker = context;
    picker->now_ms = now_ms;
    list_view_fit(&picker->list);
    preview(picker);
}

static void draw(void *context, Canvas *canvas)
{
    ThemePicker *picker = context;
    const Theme *theme = picker->screens->context.theme;
    header_draw(canvas, theme, picker->screen.title);
    list_view_draw(&picker->list, canvas, theme_content_area(theme), picker->now_ms);
}

void theme_picker_init(ThemePicker *picker, AppScreens *screens)
{
    *picker = (ThemePicker){.screens = screens, .preview = -1};
    picker->model = (ListModel){
        .context = picker, .count = count, .label = label, .detail = detail, .detail_below = true};
    list_view_init(&picker->list, &picker->model, screens->context.theme, 0);
    picker->list.background = THEME_ART_HOME;
    picker->screen = (Screen){.title = i18n_text(STR_THEME),
                              .context = picker,
                              .enter = enter,
                              .handle_key = key,
                              .update = update,
                              .draw = draw};
}
