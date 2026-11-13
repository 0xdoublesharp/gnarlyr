#include "ui/status_bar.h"

#include "core/i18n.h"
#include "core/seam.h"
#include "gfx/text.h"
#include "ui/icons.h"
#include "ui/label.h"

enum {
    FRAME = 1,
    BORDER_DIVISOR = 8,
    FILL_GAP = 1,
    NUB_DIVISOR = 5,
    BLUETOOTH_WIDTH_TENTHS = 7,
    BADGE_GAP = 1,
    TENTHS = 10,
    FULL_PERCENT = 100,
    BODY_EIGHTHS = 13,
    EIGHTHS = 8,
    TINT_STEPS = 3,
    FILL_MIN_WIDTH = 3,
    HALO = 1,
    SEGMENTS = 5
};

static int band_top(const Theme *theme, int height)
{
    int band = theme->metrics.status_height > 0 ? theme->metrics.status_height
                                                : theme->metrics.header_height;
    return (band - height) / 2;
}

static bool two_lines(const Theme *theme)
{
    return theme->metrics.status_height > 0 &&
           theme->metrics.header_height > theme->metrics.status_height;
}

static Rect icon_row(const Theme *theme)
{
    int top = theme->metrics.status_height;
    return (Rect){0, top, theme->metrics.screen_width, theme->metrics.header_height - top - 1};
}

static int icon_top(const Theme *theme)
{
    int size = theme->metrics.status_icon_size;
    Rect row = icon_row(theme);
    return two_lines(theme) ? row.y + (row.height - size) / 2 : band_top(theme, size);
}

static int nub_width(int size)
{
    return size / NUB_DIVISOR > 0 ? size / NUB_DIVISOR : 1;
}

static int border_of(Rect body)
{
    return body.height / BORDER_DIVISOR > 0 ? body.height / BORDER_DIVISOR : 1;
}

static Rect inset(Rect area, int amount)
{
    return (Rect){area.x + amount, area.y + amount, area.width - 2 * amount,
                  area.height - 2 * amount};
}

Rect status_bar_playback_area(const Theme *theme)
{
    int size = theme->metrics.status_icon_size;
    return (Rect){theme->metrics.padding, icon_top(theme), size, size};
}

Rect status_bar_bluetooth_area(const Theme *theme)
{
    Rect playback = status_bar_playback_area(theme);
    int width = playback.width * BLUETOOTH_WIDTH_TENTHS / TENTHS;
    return (Rect){playback.x + playback.width + (theme->metrics.padding >> 1), playback.y,
                  width > 0 ? width : 1, playback.height};
}

Rect status_bar_battery_body(const Theme *theme)
{
    int height = theme->status_bar_compact ? 18 : theme->metrics.status_icon_size + 2 * FRAME;
    int width = theme->status_bar_compact ? 68 : height * BODY_EIGHTHS / EIGHTHS;
    int right = theme->metrics.screen_width - theme->metrics.padding - nub_width(height);
    return (Rect){right - width, band_top(theme, height), width, height};
}

Rect status_bar_battery_area(const Theme *theme)
{
    Rect body = status_bar_battery_body(theme);
    return (Rect){body.x, body.y, theme->metrics.screen_width - theme->metrics.padding - body.x,
                  body.height};
}

Rect status_bar_lock_area(const Theme *theme)
{
    Rect bluetooth = status_bar_bluetooth_area(theme);
    return (Rect){bluetooth.x + bluetooth.width + (theme->metrics.padding >> 1), bluetooth.y,
                  bluetooth.width, bluetooth.height};
}

static IconId shuffle_badge(StatusShuffle shuffle)
{
    return shuffle == STATUS_SHUFFLE_ALBUMS ? ICON_ALBUM_SMALL : ICON_SONG_SMALL;
}

Rect status_bar_shuffle_area(const Theme *theme, StatusShuffle shuffle)
{
    Rect lock = status_bar_lock_area(theme);
    int width = icon_width(shuffle_badge(shuffle), lock.height) + BADGE_GAP +
                icon_width(ICON_SHUFFLE_SMALL, lock.height);
    return (Rect){lock.x + lock.width + (theme->metrics.padding >> 1), lock.y, width, lock.height};
}

int status_bar_battery_fill_width(uint8_t percent, int inner_width)
{
    int clamped = percent < FULL_PERCENT ? percent : FULL_PERCENT;
    int width = clamped * inner_width / FULL_PERCENT;
    int least = inner_width < FILL_MIN_WIDTH ? inner_width : FILL_MIN_WIDTH;
    return clamped > 0 && width < least ? least : width;
}

static Color tint(const Theme *theme, Color color)
{
    return color_between(color, theme->colors.header_text, 1, TINT_STEPS);
}

StatusBatteryLook status_bar_battery_look(const Theme *theme, StatusBarState state)
{
    Color text = theme->colors.header_text;
    Color danger = tint(theme, theme->colors.danger);
    bool low = !state.usb_power && state.battery_percent <= STATUS_BAR_LOW_BATTERY_PERCENT;
    bool critical = low && state.battery_percent <= STATUS_BAR_CRITICAL_BATTERY_PERCENT;
    bool lit = state.charging || state.charging_off;
    return (StatusBatteryLook){.outline = critical ? danger : text,
                               .fill = state.usb_power   ? tint(theme, theme->colors.success)
                                       : low             ? danger
                                       : state.segmented ? tint(theme, theme->colors.success)
                                                         : text,
                               .bolt = lit ? text : theme->colors.header_background,
                               .bolt_halo = lit,
                               .sign = state.charging_off ? ICON_PLUG : ICON_BOLT};
}

static void draw_outline(Canvas *canvas, Rect body, Color color)
{
    int border = border_of(body);
    canvas_fill_rect(canvas, (Rect){body.x, body.y, body.width, border}, color);
    canvas_fill_rect(canvas, (Rect){body.x, body.y + body.height - border, body.width, border},
                     color);
    canvas_fill_rect(canvas, (Rect){body.x, body.y, border, body.height}, color);
    canvas_fill_rect(canvas, (Rect){body.x + body.width - border, body.y, border, body.height},
                     color);
}

typedef struct StatusIcon {
    IconId icon;
    Rect area;
    Color color;
} StatusIcon;

static void draw_icon(Canvas *canvas, const Theme *theme, StatusIcon item)
{
    if (!theme_art_draw(theme, canvas, THEME_ART_ICON + (int)item.icon, item.area)) {
        icon_draw(canvas, item.icon, item.area, (IconInk){item.color, item.color});
    }
}

static void draw_bolt(Canvas *canvas, const Theme *theme, Rect body, StatusBatteryLook look)
{
    Rect bolt = inset(body, border_of(body));
    Canvas inside = canvas_clipped(canvas, bolt);
    for (int dy = -HALO; look.bolt_halo && dy <= HALO; dy++) {
        for (int dx = -HALO; dx <= HALO; dx++) {
            Rect shifted = {bolt.x + dx, bolt.y + dy, bolt.width, bolt.height};
            icon_draw(&inside, look.sign, shifted,
                      (IconInk){theme->colors.header_background, theme->colors.header_background});
        }
    }
    draw_icon(&inside, theme, (StatusIcon){look.sign, bolt, look.bolt});
}

static void draw_battery(Canvas *canvas, const Theme *theme, const StatusBarState *state)
{
    StatusBatteryLook look = status_bar_battery_look(theme, *state);
    Rect body = status_bar_battery_body(theme);
    Rect fill = inset(body, border_of(body) + FILL_GAP);
    if (theme->status_bar_compact) {
        fill.y = body.y + body.height - 3;
        fill.height = 2;
    }
    int step = fill.width / SEGMENTS;
    int bars = (state->battery_percent > FULL_PERCENT ? FULL_PERCENT : state->battery_percent);
    bars = (bars + FULL_PERCENT / SEGMENTS - 1) / (FULL_PERCENT / SEGMENTS);
    fill.width = status_bar_battery_fill_width(state->battery_percent, fill.width);
    draw_outline(canvas, body, look.outline);
    if (state->segmented) {
        fill.width = step > 1 ? step - 1 : 1;
        for (int bar = 0; bar < bars; bar++) {
            canvas_fill_rect(canvas, fill, look.fill);
            fill.x += step;
        }
    } else {
        canvas_fill_rect(canvas, fill, look.fill);
    }
    int terminal_height = theme->status_bar_compact ? 6 : body.height / 2;
    canvas_fill_rect(canvas,
                     (Rect){body.x + body.width, body.y + (body.height - terminal_height) / 2,
                            nub_width(body.height), terminal_height},
                     look.outline);
    if (theme->status_bar_compact) {
        char text[STATUS_BAR_PERCENT_CAPACITY];
        StringBuilder builder = string_builder_make(text, sizeof text);
        i18n_format_counts(&builder, STR_POWER_PERCENT, state->battery_percent, 0);
        label_draw(canvas, (TextStyle){theme->fonts.small, look.outline, false},
                   (Label){{body.x + 5, body.y, body.width - 21, body.height - 2}, text});
        body.x += body.width - 14;
        body.width = 12;
    }
    if (state->usb_power) {
        draw_bolt(canvas, theme, body, look);
    }
}

int status_bar_left_end(const Theme *theme, StatusBarState state)
{
    if (state.shuffle != STATUS_SHUFFLE_OFF) {
        Rect last = status_bar_shuffle_area(theme, state.shuffle);
        return last.x + last.width;
    }
    int size = theme->metrics.status_icon_size;
    int count = state.locked ? 2 : state.bluetooth != STATUS_BLUETOOTH_OFF ? 1 : 0;
    int end = theme->metrics.padding;
    if (count > 0 || state.playback != STATUS_PLAYBACK_NONE) {
        end +=
            size + count * ((theme->metrics.padding >> 1) + size * BLUETOOTH_WIDTH_TENTHS / TENTHS);
    }
    return end;
}

static int right_group_start(const Theme *theme, StatusBarState state)
{
    if (!state.battery_known) {
        return theme->metrics.screen_width - (theme->metrics.padding >> 1);
    }
    int start = status_bar_battery_body(theme).x;
    if (state.percent != NULL) {
        start -= (theme->metrics.padding >> 1) + text_width(theme->fonts.small, state.percent);
    }
    return start;
}

Rect status_bar_clock_area(const Theme *theme, StatusBarState state)
{
    Rect body = status_bar_battery_body(theme);
    int width = state.clock != NULL ? text_width(theme->fonts.small, state.clock) : 0;
    int end = right_group_start(theme, state) - (theme->metrics.padding >> 1);
    return (Rect){end - width, body.y, width, body.height};
}

bool status_bar_clock_fits(const Theme *theme, StatusBarState state)
{
    return (status_bar_items(theme, &state) & STATUS_BAR_CLOCK) != 0;
}

static void draw_shuffle(Canvas *canvas, const Theme *theme, StatusShuffle shuffle)
{
    Rect area = status_bar_shuffle_area(theme, shuffle);
    Color color = theme->colors.header_text;
    int arrows = icon_width(ICON_SHUFFLE_SMALL, area.height);
    draw_icon(canvas, theme,
              (StatusIcon){shuffle_badge(shuffle),
                           (Rect){area.x, area.y, area.width - BADGE_GAP - arrows, area.height},
                           color});
    draw_icon(canvas, theme,
              (StatusIcon){ICON_SHUFFLE_SMALL,
                           (Rect){area.x + area.width - arrows, area.y, arrows, area.height},
                           color});
}

static void draw_left_icons(Canvas *canvas, const Theme *theme, const StatusBarState *state)
{
    Rect icon = status_bar_playback_area(theme);
    Color color = theme->colors.header_text;
    if (state->playback == STATUS_PLAYBACK_PLAYING) {
        draw_icon(canvas, theme, (StatusIcon){ICON_PLAY, icon, color});
    } else if (state->playback == STATUS_PLAYBACK_PAUSED) {
        draw_icon(canvas, theme, (StatusIcon){ICON_PAUSE, icon, color});
    }
    if (state->bluetooth != STATUS_BLUETOOTH_OFF) {
        Color tint =
            state->bluetooth == STATUS_BLUETOOTH_CONNECTED ? color : theme->colors.text_tertiary;
        draw_icon(canvas, theme,
                  (StatusIcon){ICON_BLUETOOTH_SMALL, status_bar_bluetooth_area(theme), tint});
    }
    if (state->locked) {
        draw_icon(canvas, theme, (StatusIcon){ICON_LOCK, status_bar_lock_area(theme), color});
    }
    if (state->shuffle != STATUS_SHUFFLE_OFF) {
        draw_shuffle(canvas, theme, state->shuffle);
    }
}

static int item_width(const Theme *theme, const StatusBarState *state, uint8_t item)
{
    if (theme->status_bar_compact && item == STATUS_BAR_PERCENT) {
        return 0;
    }
    const char *text = item == STATUS_BAR_VOLUME     ? state->volume
                       : item == STATUS_BAR_POSITION ? state->position
                       : item == STATUS_BAR_CLOCK    ? state->clock
                                                     : state->percent;
    int width = text != NULL ? text_width(theme->fonts.small, text) : 0;
    int icon = theme->metrics.status_icon_size;
    if (item == STATUS_BAR_REPEAT) {
        return state->repeat != 0
                   ? icon + (state->repeat == 1 ? BADGE_GAP + icon_width(ICON_SONG_SMALL, icon) : 0)
                   : 0;
    }
    return width + (item == STATUS_BAR_VOLUME && width > 0 ? icon + BADGE_GAP : 0);
}

static void join_line_two(StatusBarLayout *layout, int gap, int end, int icons_end)
{
    Rect *areas = layout->areas;
    uint8_t items = layout->items;
    int edge = (items & STATUS_BAR_CLOCK) != 0     ? areas[2].x
               : (items & STATUS_BAR_PERCENT) != 0 ? areas[4].x
                                                   : end - gap;
    if ((items & STATUS_BAR_POSITION) != 0) {
        areas[1].x = edge - gap - areas[1].width;
        edge = areas[1].x;
    }
    int last = (items & STATUS_BAR_VOLUME) != 0   ? areas[0].x + areas[0].width
               : (items & STATUS_BAR_REPEAT) != 0 ? areas[3].x + areas[3].width
                                                  : icons_end;
    layout->shift = edge - gap - last;
    areas[0].x += layout->shift;
    areas[3].x += layout->shift;
}

CORE_SEAM void status_bar_layout(const Theme *theme, const StatusBarState *state,
                                 StatusBarLayout *layout)
{
    layout->items = 0;
    layout->shift = 0;
    bool split = two_lines(theme);
    int gap = theme->metrics.padding >> 1;
    int left = status_bar_left_end(theme, *state);
    int icons_end = left;
    int begin = left + gap;
    Rect body = status_bar_battery_body(theme);
    Rect row = split ? icon_row(theme) : body;
    int end = state->battery_known && !split ? body.x : theme->metrics.screen_width - gap;
    int right = end;
    int centre_width = split ? 0 : state->title_width;
    int reserve = split ? state->title_width + gap : 0;
    for (int index = 0; index < 5; index++) {
        uint8_t item = (uint8_t)(1u << index);
        int width = item_width(theme, state, item);
        bool centred = item == STATUS_BAR_POSITION && !split;
        bool trailing =
            item == STATUS_BAR_CLOCK || item == STATUS_BAR_PERCENT || item == STATUS_BAR_POSITION;
        int x = centred    ? (theme->metrics.screen_width - width) >> 1
                : trailing ? end - gap - width
                           : begin;
        layout->areas[index] = (Rect){x, row.y, width, row.height};
        if (width == 0 || (centred && state->title_width != 0) ||
            (item == STATUS_BAR_PERCENT && !state->battery_known)) {
            continue;
        }
        int l = left;
        int r = right;
        int c = centre_width;
        if (centred) {
            c = width;
        } else if (trailing) {
            r -= width + gap;
        } else {
            l += width + gap;
        }
        int start = (theme->metrics.screen_width - c) >> 1;
        int pad = theme->metrics.padding;
        bool fits = c > 0 ? l + pad <= start && start + c + pad <= r : l + pad + reserve <= r;
        if (fits) {
            layout->items |= item;
            left = l;
            right = r;
            centre_width = c;
        }
    }
    if ((layout->items & STATUS_BAR_REPEAT) != 0) {
        layout->areas[0].x += layout->areas[3].width + gap;
    }
    if ((layout->items & STATUS_BAR_PERCENT) != 0) {
        layout->areas[2].x -= layout->areas[4].width + gap;
    }
    if (split) {
        join_line_two(layout, gap, end, icons_end);
    }
}

uint8_t status_bar_items(const Theme *theme, const StatusBarState *state)
{
    StatusBarLayout layout;
    status_bar_layout(theme, state, &layout);
    return layout.items;
}

Rect status_bar_item_area(const Theme *theme, const StatusBarState *state, uint8_t item)
{
    StatusBarLayout layout;
    status_bar_layout(theme, state, &layout);
    int index = 0;
    while ((item >>= 1) != 0) {
        index++;
    }
    return layout.areas[index];
}

CORE_SEAM void status_bar_draw(Canvas *canvas, const Theme *theme, StatusBarState state)
{
    StatusBarLayout layout;
    status_bar_layout(theme, &state, &layout);
    uint8_t items = layout.items;
    Rect clip = canvas->clip;
    Canvas moved = canvas_shifted(canvas, layout.shift,
                                  (Rect){clip.x - layout.shift, clip.y, clip.width, clip.height});
    draw_left_icons(&moved, theme, &state);
    if (state.battery_known) {
        draw_battery(canvas, theme, &state);
    }
    for (int index = 0; index < 5; index++) {
        uint8_t item = (uint8_t)(1u << index);
        if ((items & item) == 0) {
            continue;
        }
        Rect area = layout.areas[index];
        if (item == STATUS_BAR_REPEAT || item == STATUS_BAR_VOLUME) {
            Rect icon = {area.x, icon_top(theme), theme->metrics.status_icon_size,
                         theme->metrics.status_icon_size};
            draw_icon(canvas, theme,
                      (StatusIcon){item == STATUS_BAR_REPEAT ? ICON_REPEAT : ICON_SPEAKER, icon,
                                   theme->colors.header_text});
            area.x += icon.width + BADGE_GAP;
            if (item == STATUS_BAR_REPEAT && state.repeat == 1) {
                icon.x = area.x;
                icon.width = icon_width(ICON_SONG_SMALL, icon.height);
                draw_icon(canvas, theme,
                          (StatusIcon){ICON_SONG_SMALL, icon, theme->colors.header_text});
            }
        }
        TextStyle style = {theme->fonts.small, theme->colors.header_text, false};
        if (item == STATUS_BAR_PERCENT && !state.usb_power &&
            state.battery_percent <= STATUS_BAR_LOW_BATTERY_PERCENT) {
            style.color = tint(theme, theme->colors.danger);
        }
        const char *text = item == STATUS_BAR_VOLUME     ? state.volume
                           : item == STATUS_BAR_POSITION ? state.position
                           : item == STATUS_BAR_CLOCK    ? state.clock
                           : item == STATUS_BAR_PERCENT  ? state.percent
                                                         : "";
        text_draw(canvas, style, (Point){area.x, label_baseline(style.font, area)}, text);
    }
}

void status_bar_position_text(StringBuilder *builder, StatusQueuePosition position)
{
    if (position.total == 0) {
        return;
    }
    if (position.prefix != NULL && position.prefix[0] != '\0') {
        string_builder_append(builder, position.prefix);
        string_builder_append_char(builder, ' ');
    }
    i18n_format_counts(builder, STR_STATUS_PLACE, position.number, position.total);
}
