#include "home_editor.h"

#include <stddef.h>

#include "core/string_builder.h"
#include "gfx/text.h"
#include "hal/clock.h"
#include "ui/footer_hints.h"
#include "ui/header.h"
#include "ui/list_navigation.h"
#include "ui/navigation.h"

static int row_count(const void *context)
{
    (void)context;
    return HOME_EDITOR_ROWS;
}

static bool is_item_row(int index)
{
    return index < HOME_EDITOR_ITEM_COUNT;
}

static const HomeItem *item_at(const HomeEditor *editor, int index)
{
    return &editor->config.items[editor->order[index]];
}

static bool item_hidden(const HomeEditor *editor, int item)
{
    return ((editor->config.settings->home_hidden >> item) & 1u) != 0;
}

static const char *row_label(const void *context, int index)
{
    return is_item_row(index) ? i18n_text(item_at(context, index)->label)
                              : i18n_text(STR_HOME_RESET);
}

static const char *row_detail(const void *context, int index)
{
    const HomeEditor *editor = context;
    if (!is_item_row(index)) {
        return NULL;
    }
    int item = editor->order[index];
    StringId detail = editor->moving && index == editor->list.selected ? STR_HOME_PLACE
                      : item == SETTINGS_HOME_SETTINGS_ITEM            ? STR_HOME_ALWAYS
                      : item_hidden(editor, item)                      ? STR_HOME_HIDDEN
                                                                       : STR_HOME_SHOWN;
    return i18n_text(detail);
}

static ListRowRole row_role(const void *context, int index)
{
    (void)context;
    return is_item_row(index) ? LIST_ROW_PLAIN : LIST_ROW_DANGER_DIVIDED;
}

static IconId row_icon(const void *context, int index)
{
    return is_item_row(index) ? item_at(context, index)->icon : ICON_NONE;
}

static void announce(HomeEditor *editor, StringId pattern, int item)
{
    StringBuilder message = string_builder_make(editor->message, sizeof editor->message);
    i18n_format_text(&message, pattern, i18n_text(editor->config.items[item].label));
    toast_show(editor->config.toast, editor->message, hal_clock_milliseconds());
}

static void commit(HomeEditor *editor, StringId pattern, int item)
{
    Settings *settings = editor->config.settings;
    for (int index = 0; index < HOME_EDITOR_ITEM_COUNT; index++) {
        settings->home_order[index] = editor->order[index];
    }
    settings->home_order[HOME_EDITOR_ITEM_COUNT] = SETTINGS_HOME_NOW_PLAYING_ITEM;
    if (editor->config.changed(editor->config.context)) {
        announce(editor, pattern, item);
    }
}

static void sync_order(HomeEditor *editor)
{
    int count = 0;
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        int item = editor->config.settings->home_order[index];
        if (item != SETTINGS_HOME_NOW_PLAYING_ITEM) {
            editor->order[count++] = (uint8_t)item;
        }
    }
}

static void begin_move(HomeEditor *editor, int index)
{
    editor->moving = true;
    editor->picked_from = (int8_t)index;
    list_view_select(&editor->list, index, editor->now_ms);
}

static void shift(HomeEditor *editor, int direction)
{
    int from = editor->list.selected;
    int to = from + direction;
    if (to < 0 || to >= HOME_EDITOR_ITEM_COUNT) {
        return;
    }
    uint8_t item = editor->order[from];
    editor->order[from] = editor->order[to];
    editor->order[to] = item;
    list_view_select(&editor->list, to, editor->now_ms);
}

static void finish_move(HomeEditor *editor, bool place)
{
    int item = editor->order[editor->list.selected];
    editor->moving = false;
    if (place) {
        commit(editor, STR_HOME_PLACED, item);
        return;
    }
    sync_order(editor);
    list_view_select(&editor->list, editor->picked_from, editor->now_ms);
    announce(editor, STR_HOME_MOVE_CANCELLED, item);
}

static ScreenAction handle_move(HomeEditor *editor, KeyEvent event)
{
    switch (navigation_from_key(event).intent) {
    case NAVIGATION_NEXT:
        shift(editor, 1);
        break;
    case NAVIGATION_PREVIOUS:
        shift(editor, -1);
        break;
    case NAVIGATION_SELECT:
    case NAVIGATION_BACK:
        finish_move(editor, navigation_from_key(event).intent == NAVIGATION_SELECT);
        break;
    case NAVIGATION_HOME:
        return screen_action_pop_to_root();
    case NAVIGATION_OPTIONS:
    case NAVIGATION_NONE:
        break;
    }
    return screen_action_none();
}

static void toggle_hidden(HomeEditor *editor, int item)
{
    editor->config.settings->home_hidden ^= (uint16_t)(1u << item);
    commit(editor, item_hidden(editor, item) ? STR_HOME_WAS_HIDDEN : STR_HOME_WAS_SHOWN, item);
}

static ScreenAction reset(HomeEditor *editor)
{
    Settings defaults = settings_defaults();
    editor->config.settings->home_hidden = defaults.home_hidden;
    for (int index = 0; index < HOME_EDITOR_ITEM_COUNT; index++) {
        editor->order[index] = defaults.home_order[index];
    }
    commit(editor, STR_HOME_WAS_RESET, SETTINGS_HOME_SETTINGS_ITEM);
    return screen_action_none();
}

static ScreenAction select_row(void *context, int index)
{
    HomeEditor *editor = context;
    if (!is_item_row(index)) {
        return reset(editor);
    }
    int item = editor->order[index];
    if (item != SETTINGS_HOME_SETTINGS_ITEM) {
        toggle_hidden(editor, item);
    }
    return screen_action_none();
}

static ScreenAction pick_row(void *context, int index)
{
    if (is_item_row(index)) {
        begin_move(context, index);
    }
    return screen_action_none();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    HomeEditor *editor = context;
    editor->now_ms = now_ms;
    if (editor->moving) {
        return handle_move(editor, event);
    }
    ListSelection selection = {.select = select_row, .hold = pick_row, .context = editor};
    return list_navigation_handle(&editor->list, event, now_ms, selection);
}

static void enter(void *context, uint32_t now_ms)
{
    HomeEditor *editor = context;
    editor->now_ms = now_ms;
    editor->moving = false;
    sync_order(editor);
    list_view_restart_marquee(&editor->list, now_ms);
}

static FooterHints editor_hints(const HomeEditor *editor, FooterHint *hints)
{
    int count = 0;
    if (editor->moving) {
        hints[count++] = (FooterHint){FOOTER_KEY_PLAY, STR_HOME_PLACE};
        hints[count++] = (FooterHint){FOOTER_KEY_BACK, STR_CANCEL};
    } else if (is_item_row(editor->list.selected)) {
        int item = editor->order[editor->list.selected];
        if (item != SETTINGS_HOME_SETTINGS_ITEM) {
            StringId action = item_hidden(editor, item) ? STR_HOME_SHOW : STR_HOME_HIDE;
            hints[count++] = (FooterHint){FOOTER_KEY_PLAY, (uint16_t)action};
        }
        hints[count++] = (FooterHint){FOOTER_KEY_HOLD_PLAY, STR_HOME_MOVE};
    }
    return (FooterHints){.items = hints, .count = (uint8_t)count};
}

static Rect editor_footer(const HomeEditor *editor, FooterHints hints)
{
    const Theme *theme = editor->config.theme;
    return footer_hints_area(theme, theme_content_area(theme), hints);
}

static void update(void *context, uint32_t now_ms)
{
    HomeEditor *editor = context;
    editor->now_ms = now_ms;
    const Theme *theme = editor->config.theme;
    FooterHint hints[2];
    Rect footer = editor_footer(editor, editor_hints(editor, hints));
    list_view_fit_rows(&editor->list,
                       list_view_rows_in(&editor->list, footer.y - theme_content_area(theme).y));
    const Font *font = theme->fonts.header;
    editor->screen.header_title_width =
        editor->moving ? (uint16_t)text_width(font, i18n_text(STR_HOME_MOVING)) : 0;
}

static void draw(void *context, Canvas *canvas)
{
    const HomeEditor *editor = context;
    const Theme *theme = editor->config.theme;
    header_draw(canvas, theme, editor->moving ? i18n_text(STR_HOME_MOVING) : editor->screen.title);
    FooterHint storage[2];
    FooterHints hints = editor_hints(editor, storage);
    Rect footer = editor_footer(editor, hints);
    Rect content = theme_content_area(theme);
    content.height = footer.y - content.y;
    list_view_draw(&editor->list, canvas, content, editor->now_ms);
    footer_hints_draw(canvas, theme, footer, hints);
}

Screen *home_editor_init(HomeEditor *editor, HomeEditorConfig config)
{
    *editor = (HomeEditor){.config = config};
    editor->model = (ListModel){.context = editor,
                                .count = row_count,
                                .label = row_label,
                                .detail = row_detail,
                                .role = row_role,
                                .icon = row_icon,
                                .detail_below = true};
    list_view_init(&editor->list, &editor->model, config.theme, 0);
    editor->screen = (Screen){.title = i18n_text(STR_HOME_SCREEN),
                              .context = editor,
                              .enter = enter,
                              .handle_key = handle_key,
                              .update = update,
                              .draw = draw};
    return &editor->screen;
}
