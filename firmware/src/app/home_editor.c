#include "home_editor.h"

#include <stddef.h>

#include "core/string_builder.h"
#include "hal/clock.h"
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
    return index < SETTINGS_HOME_ITEM_COUNT;
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

static bool row_opens(const void *context, int index)
{
    const HomeEditor *editor = context;
    return is_item_row(index) && !editor->moving;
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
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        settings->home_order[index] = editor->order[index];
    }
    if (editor->config.changed(editor->config.context)) {
        announce(editor, pattern, item);
    }
}

static void sync_order(HomeEditor *editor)
{
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        editor->order[index] = editor->config.settings->home_order[index];
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
    if (to < 0 || to >= SETTINGS_HOME_ITEM_COUNT) {
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

static ScreenAction toggle_hidden(void *context)
{
    HomeEditor *editor = context;
    int item = editor->order[editor->target];
    editor->config.settings->home_hidden ^= (uint16_t)(1u << item);
    commit(editor, item_hidden(editor, item) ? STR_HOME_WAS_HIDDEN : STR_HOME_WAS_SHOWN, item);
    return screen_action_pop();
}

static ScreenAction move_from_actions(void *context)
{
    HomeEditor *editor = context;
    editor->pick_pending = true;
    return screen_action_pop();
}

static ScreenAction open_actions(HomeEditor *editor, int index)
{
    int item = editor->order[index];
    int count = 0;
    editor->target = (int8_t)index;
    if (item != SETTINGS_HOME_SETTINGS_ITEM) {
        StringId label = item_hidden(editor, item) ? STR_HOME_SHOW : STR_HOME_HIDE;
        editor->action_items[count++] =
            (MenuItem){.label = i18n_text(label), .select = toggle_hidden, .context = editor};
    }
    editor->action_items[count++] = (MenuItem){
        .label = i18n_text(STR_HOME_MOVE), .select = move_from_actions, .context = editor};
    MenuConfig config = {.title = i18n_text(editor->config.items[item].label),
                         .items = editor->action_items,
                         .item_count = count,
                         .theme = editor->config.theme};
    return screen_action_push(menu_screen_init(&editor->actions, config));
}

static ScreenAction reset(HomeEditor *editor)
{
    Settings defaults = settings_defaults();
    editor->config.settings->home_hidden = defaults.home_hidden;
    for (int index = 0; index < SETTINGS_HOME_ITEM_COUNT; index++) {
        editor->order[index] = defaults.home_order[index];
    }
    commit(editor, STR_HOME_WAS_RESET, SETTINGS_HOME_SETTINGS_ITEM);
    return screen_action_none();
}

static ScreenAction select_row(void *context, int index)
{
    return is_item_row(index) ? open_actions(context, index) : reset(context);
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
    if (editor->pick_pending) {
        editor->pick_pending = false;
        begin_move(editor, editor->target);
    }
    list_view_restart_marquee(&editor->list, now_ms);
}

static void update(void *context, uint32_t now_ms)
{
    HomeEditor *editor = context;
    editor->now_ms = now_ms;
}

static void draw(void *context, Canvas *canvas)
{
    const HomeEditor *editor = context;
    const Theme *theme = editor->config.theme;
    header_draw(canvas, theme, editor->moving ? i18n_text(STR_HOME_MOVING) : editor->screen.title);
    list_view_draw(&editor->list, canvas, theme_content_area(theme), editor->now_ms);
}

Screen *home_editor_init(HomeEditor *editor, HomeEditorConfig config)
{
    *editor = (HomeEditor){.config = config};
    editor->model = (ListModel){.context = editor,
                                .count = row_count,
                                .label = row_label,
                                .detail = row_detail,
                                .opens = row_opens,
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
