#include "bluetooth_screen.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"
#include "ui/navigation.h"

enum {
    FOOTER_LINE_PITCH = 15,
    HINT_MAX_LINES = 3,
    FOOTER_GAP = 4
};

typedef enum RowKind {
    ROW_POWER,
    ROW_KNOWN,
    ROW_SEARCH,
    ROW_NEARBY
} RowKind;

static const char SEPARATOR[] = " · ";

static bool same_address(const HalBluetoothDevice *first, const HalBluetoothDevice *second)
{
    for (int index = 0; index < HAL_BLUETOOTH_ADDRESS_SIZE; index++) {
        if (first->address[index] != second->address[index]) {
            return false;
        }
    }
    return true;
}

static bool enabled(const BluetoothScreen *bluetooth)
{
    return bluetooth->link->saved.enabled;
}

static int known_count(const BluetoothScreen *bluetooth)
{
    return enabled(bluetooth) ? bluetooth->link->saved.count : 0;
}

static int search_row(const BluetoothScreen *bluetooth)
{
    return 1 + known_count(bluetooth);
}

static RowKind row_kind(const BluetoothScreen *bluetooth, int index)
{
    if (index == 0) {
        return ROW_POWER;
    }
    if (index < search_row(bluetooth)) {
        return ROW_KNOWN;
    }
    return index == search_row(bluetooth) ? ROW_SEARCH : ROW_NEARBY;
}

static const HalBluetoothDevice *nearby_device(const BluetoothScreen *bluetooth, int index)
{
    int position = index - search_row(bluetooth) - 1;
    return &hal_bluetooth_status()->devices[bluetooth->nearby[position]];
}

static int row_count(const void *context)
{
    const BluetoothScreen *bluetooth = context;
    if (!enabled(bluetooth)) {
        return 1;
    }
    return search_row(bluetooth) + 1 + bluetooth->nearby_count;
}

static const char *row_label(const void *context, int index)
{
    const BluetoothScreen *bluetooth = context;
    switch (row_kind(bluetooth, index)) {
    case ROW_POWER:
        return i18n_text(STR_BLUETOOTH);
    case ROW_KNOWN:
        return bluetooth->link->saved.devices[index - 1].name;
    case ROW_SEARCH:
        return i18n_text(STR_BT_PAIR_NEW);
    default:
        return nearby_device(bluetooth, index)->name;
    }
}

static const char *known_value(const BluetoothScreen *bluetooth, int index)
{
    switch (bluetooth_link_device_state(bluetooth->link, index)) {
    case BLUETOOTH_DEVICE_PLAYING:
        return i18n_text(STR_PLAYING);
    case BLUETOOTH_DEVICE_CONNECTED:
        return i18n_text(STR_BT_CONNECTED);
    case BLUETOOTH_DEVICE_CONNECTING:
        return i18n_text(STR_BT_CONNECTING);
    default:
        return bluetooth_link_nearby(bluetooth->link, index) ? i18n_text(STR_BT_NEARBY) : NULL;
    }
}

static bool scanning(const BluetoothScreen *bluetooth)
{
    return bluetooth_link_searching(bluetooth->link);
}

static const char *nearby_value(const BluetoothScreen *bluetooth, int index)
{
    if (!bluetooth->pairing || !same_address(nearby_device(bluetooth, index), &bluetooth->paired)) {
        return NULL;
    }
    const BluetoothLink *link = bluetooth->link;
    bool trying = link->attempting || link->has_target || link->paired_seen;
    return i18n_text(trying ? STR_BT_CONNECTING : STR_BT_DID_NOT_CONNECT);
}

static const char *row_value(const void *context, int index)
{
    const BluetoothScreen *bluetooth = context;
    switch (row_kind(bluetooth, index)) {
    case ROW_POWER:
        return i18n_text(enabled(bluetooth) ? STR_ON : STR_OFF);
    case ROW_KNOWN:
        return known_value(bluetooth, index - 1);
    case ROW_SEARCH:
        return scanning(bluetooth) ? i18n_text(STR_BT_SEARCHING) : NULL;
    default:
        return nearby_value(bluetooth, index);
    }
}

static void find_nearby(BluetoothScreen *bluetooth)
{
    const HalBluetoothStatus *status = hal_bluetooth_status();
    bluetooth->nearby_count = 0;
    if (!enabled(bluetooth) || bluetooth->link->connected) {
        return;
    }
    for (uint8_t index = 0; index < status->device_count; index++) {
        if (!bluetooth_link_knows(bluetooth->link, &status->devices[index])) {
            bluetooth->nearby[bluetooth->nearby_count++] = index;
        }
    }
}

static StringId known_hint(const BluetoothScreen *bluetooth, int index)
{
    switch (bluetooth_link_device_state(bluetooth->link, index)) {
    case BLUETOOTH_DEVICE_PLAYING:
    case BLUETOOTH_DEVICE_CONNECTED:
        return STR_BT_HINT_DISCONNECT;
    case BLUETOOTH_DEVICE_CONNECTING:
        return STR_BT_HINT_CONNECTING;
    default:
        return STR_BT_HINT_CONNECT;
    }
}

static StringId hint_id(const BluetoothScreen *bluetooth)
{
    int selected = bluetooth->list.selected;
    if (enabled(bluetooth) && bluetooth->link->dozing) {
        return STR_BT_HINT_DOZING;
    }
    switch (row_kind(bluetooth, selected)) {
    case ROW_POWER:
        return enabled(bluetooth) ? STR_BT_HINT_TURN_OFF : STR_BT_HINT_TURN_ON;
    case ROW_KNOWN:
        return known_hint(bluetooth, selected - 1);
    case ROW_SEARCH:
        return scanning(bluetooth) ? STR_BT_HINT_SEARCHING : STR_BT_HINT_SEARCH;
    default:
        return STR_BT_HINT_PAIR;
    }
}

static const char *hint(const BluetoothScreen *bluetooth)
{
    return i18n_text(hint_id(bluetooth));
}

static int hint_width(const BluetoothScreen *bluetooth)
{
    return bluetooth->theme->metrics.screen_width - 2 * bluetooth->theme->metrics.padding;
}

static uint8_t widest_hint_lines(const BluetoothScreen *bluetooth)
{
    static const StringId HINTS[] = {
        STR_BT_HINT_DISCONNECT, STR_BT_HINT_CONNECTING, STR_BT_HINT_CONNECT,
        STR_BT_HINT_TURN_OFF,   STR_BT_HINT_TURN_ON,    STR_BT_HINT_SEARCHING,
        STR_BT_HINT_SEARCH,     STR_BT_HINT_PAIR,       STR_BT_HINT_DOZING};
    int lines = 1;
    for (size_t index = 0; index < sizeof HINTS / sizeof HINTS[0]; index++) {
        int needed = label_wrap_count(bluetooth->theme->fonts.small, i18n_text(HINTS[index]),
                                      hint_width(bluetooth));
        lines = needed > lines ? needed : lines;
    }
    return (uint8_t)(lines < HINT_MAX_LINES ? lines : HINT_MAX_LINES);
}

static const char *state_text(HalBluetoothState state)
{
    switch (state) {
    case HAL_BLUETOOTH_STARTING:
        return "Starting";
    case HAL_BLUETOOTH_NO_REPLY:
        return "No reply";
    case HAL_BLUETOOTH_READY:
        return "Ready";
    case HAL_BLUETOOTH_SCANNING:
        return "Scanning";
    case HAL_BLUETOOTH_CONNECTING:
        return "Connecting";
    case HAL_BLUETOOTH_CONNECTED:
        return "Connected";
    default:
        return "Off";
    }
}

static void write_state(const HalBluetoothStatus *status, char *text)
{
    StringBuilder builder = string_builder_make(text, BLUETOOTH_SCREEN_LINE_CAPACITY);
    string_builder_append(&builder, state_text(status->state));
    if (status->streaming) {
        string_builder_append(&builder, SEPARATOR);
        string_builder_append(&builder, "Streaming");
    }
    if (status->version != '\0') {
        string_builder_append(&builder, SEPARATOR);
        string_builder_append_char(&builder, 'v');
        string_builder_append_char(&builder, status->version);
    }
    if (status->link_status != '\0') {
        string_builder_append(&builder, SEPARATOR);
        string_builder_append(&builder, "status ");
        string_builder_append_char(&builder, status->link_status);
    }
    if (status->pairing == HAL_BLUETOOTH_PAIRING_FAILED) {
        string_builder_append(&builder, SEPARATOR);
        string_builder_append(&builder, "Pairing failed");
    }
}

static void write_link(const HalBluetoothLink *link, char *text)
{
    StringBuilder builder = string_builder_make(text, BLUETOOTH_SCREEN_LINE_CAPACITY);
    string_builder_append(&builder, "RX ");
    string_builder_append_unsigned(&builder, link->received);
    string_builder_append(&builder, SEPARATOR);
    string_builder_append_unsigned(&builder, link->frames);
    string_builder_append(&builder, " frames");
    string_builder_append(&builder, SEPARATOR);
    string_builder_append(&builder, "OE ");
    string_builder_append_unsigned(&builder, link->overruns);
    string_builder_append(&builder, SEPARATOR);
    string_builder_append(&builder, "FIFO ");
    string_builder_append_unsigned(&builder, link->fifo_peak);
    string_builder_append_char(&builder, '/');
    string_builder_append_unsigned(&builder, link->fifo_depth);
}

static void write_events(const HalBluetoothLink *link, char *text, uint8_t first)
{
    StringBuilder builder = string_builder_make(text, BLUETOOTH_SCREEN_LINE_CAPACITY);
    if (first == 0) {
        string_builder_append(&builder, link->event_count == 0 ? "Msgs none" : "Msgs");
    }
    uint8_t last = first + BLUETOOTH_SCREEN_EVENTS_PER_LINE;
    if (last > link->event_count) {
        last = link->event_count;
    }
    for (uint8_t index = first; index < last; index++) {
        string_builder_append(&builder, link->events[index][0] == '>' ? " " : " C");
        string_builder_append_char(&builder, link->events[index][0]);
        string_builder_append_char(&builder, link->events[index][1]);
        if (link->gaps[index] > 0) {
            string_builder_append_char(&builder, '+');
            string_builder_append_unsigned(&builder, link->gaps[index]);
        }
    }
}

static void refresh(BluetoothScreen *bluetooth)
{
    const HalBluetoothStatus *status = hal_bluetooth_status();
    if (bluetooth->pairing && (bluetooth->link->connected || !enabled(bluetooth))) {
        bluetooth->pairing = false;
    }
    find_nearby(bluetooth);
    int count = row_count(bluetooth);
    if (bluetooth->list.selected >= count) {
        list_view_select(&bluetooth->list, count - 1, bluetooth->now_ms);
    }
    write_state(status, bluetooth->detail_lines[0]);
    write_link(&status->link, bluetooth->detail_lines[1]);
    write_events(&status->link, bluetooth->detail_lines[2], 0);
    write_events(&status->link, bluetooth->detail_lines[3], BLUETOOTH_SCREEN_EVENTS_PER_LINE);
}

static int footer_lines(const BluetoothScreen *bluetooth)
{
    return bluetooth->details ? BLUETOOTH_SCREEN_LINE_COUNT : 1;
}

static Rect list_area(const BluetoothScreen *bluetooth)
{
    Rect content = theme_content_area(bluetooth->theme);
    int line_height = font_line_height(bluetooth->theme->fonts.small);
    int details = footer_lines(bluetooth) - 1;
    content.height -= FOOTER_GAP + bluetooth->hint_lines * line_height +
                      (details > 0 ? (details - 1) * FOOTER_LINE_PITCH + line_height : 0);
    return content;
}

static void enter(void *context, uint32_t now_ms)
{
    BluetoothScreen *bluetooth = context;
    bluetooth->now_ms = now_ms;
    bluetooth->pairing = false;
    bluetooth->hint_lines = widest_hint_lines(bluetooth);
    bluetooth->list.visible_rows =
        list_area(bluetooth).height / bluetooth->theme->metrics.row_height;
    list_view_select(&bluetooth->list, 0, now_ms);
    bluetooth_link_browse(bluetooth->link, true, now_ms);
    refresh(bluetooth);
}

static ScreenAction choose(void *context, int index)
{
    BluetoothScreen *bluetooth = context;
    BluetoothLink *link = bluetooth->link;
    uint32_t now_ms = bluetooth->now_ms;
    bool woke = bluetooth_link_reconnect(link, now_ms);
    switch (row_kind(bluetooth, index)) {
    case ROW_POWER:
        bluetooth->pairing = false;
        if (!woke) {
            bluetooth_link_set_enabled(link, !enabled(bluetooth), now_ms);
        }
        break;
    case ROW_KNOWN:
        bluetooth->pairing = false;
        bluetooth_link_choose(link, index - 1, now_ms);
        break;
    case ROW_SEARCH:
        bluetooth->pairing = false;
        bluetooth_link_search(link, now_ms);
        break;
    default:
        bluetooth->paired = *nearby_device(bluetooth, index);
        bluetooth->pairing = true;
        bluetooth_link_pair(link, &bluetooth->paired, now_ms);
        break;
    }
    refresh(bluetooth);
    return screen_action_none();
}

static ScreenAction hold(void *context, int index)
{
    BluetoothScreen *bluetooth = context;
    if (row_kind(bluetooth, index) == ROW_KNOWN) {
        bluetooth_link_forget(bluetooth->link, index - 1);
        refresh(bluetooth);
    }
    return screen_action_none();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    BluetoothScreen *bluetooth = context;
    bluetooth->now_ms = now_ms;
    ListSelection selection = {.select = choose, .hold = hold, .context = bluetooth};
    ScreenAction action = list_navigation_handle(&bluetooth->list, event, now_ms, selection);
    if (action.type == SCREEN_ACTION_POP || action.type == SCREEN_ACTION_POP_TO_ROOT) {
        bluetooth_link_browse(bluetooth->link, false, now_ms);
    } else {
        refresh(bluetooth);
    }
    return action;
}

static void update(void *context, uint32_t now_ms)
{
    BluetoothScreen *bluetooth = context;
    bluetooth->now_ms = now_ms;
    refresh(bluetooth);
}

static Rect draw_hint(const BluetoothScreen *bluetooth, Canvas *canvas, Rect footer)
{
    const Theme *theme = bluetooth->theme;
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    int line_height = font_line_height(style.font);
    int needed = label_wrap_count(style.font, hint(bluetooth), hint_width(bluetooth));
    int spare = needed < bluetooth->hint_lines ? bluetooth->hint_lines - needed : 0;
    Rect area = {footer.x + theme->metrics.padding, footer.y + FOOTER_GAP + spare * line_height,
                 hint_width(bluetooth), (bluetooth->hint_lines - spare) * line_height};
    label_draw_paragraph(canvas, style, (Label){.area = area, .text = hint(bluetooth)});
    return (Rect){area.x, area.y + area.height, area.width, line_height};
}

static void draw(void *context, Canvas *canvas)
{
    const BluetoothScreen *bluetooth = context;
    const Theme *theme = bluetooth->theme;
    Rect list = list_area(bluetooth);
    header_draw(canvas, theme, bluetooth->screen.title);
    list_view_draw(&bluetooth->list, canvas, list, bluetooth->now_ms);
    Rect footer = {list.x, list.y + list.height, list.width,
                   theme_content_area(theme).height - list.height};
    canvas_fill_rect(canvas, footer, theme->colors.background);
    Rect area = draw_hint(bluetooth, canvas, footer);
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_tertiary};
    for (int index = 1; index < footer_lines(bluetooth); index++, area.y += FOOTER_LINE_PITCH) {
        label_draw(canvas, style,
                   (Label){.area = area, .text = bluetooth->detail_lines[index - 1]});
    }
}

Screen *bluetooth_screen_init(BluetoothScreen *bluetooth, BluetoothLink *link, const Theme *theme)
{
    *bluetooth = (BluetoothScreen){.theme = theme, .link = link, .hint_lines = 1};
    bluetooth->model = (ListModel){
        .context = bluetooth, .count = row_count, .label = row_label, .detail = row_value};
    list_view_init(&bluetooth->list, &bluetooth->model, theme, 0);
    bluetooth->screen = (Screen){
        .title = i18n_text(STR_BLUETOOTH),
        .stays_on_wake = true,
        .context = bluetooth,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &bluetooth->screen;
}

int bluetooth_screen_row_count(const BluetoothScreen *bluetooth)
{
    return row_count(bluetooth);
}

const char *bluetooth_screen_row(const BluetoothScreen *bluetooth, int index)
{
    return row_label(bluetooth, index);
}

const char *bluetooth_screen_row_value(const BluetoothScreen *bluetooth, int index)
{
    const char *value = row_value(bluetooth, index);
    return value != NULL ? value : "";
}

const char *bluetooth_screen_line(const BluetoothScreen *bluetooth, int index)
{
    return index == BLUETOOTH_SCREEN_HINT_LINE ? hint(bluetooth)
                                               : bluetooth->detail_lines[index - 1];
}

const char *bluetooth_screen_summary(const void *bluetooth)
{
    const BluetoothScreen *screen = bluetooth;
    return bluetooth_link_summary(screen->link);
}
