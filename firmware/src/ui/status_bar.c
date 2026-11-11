#include "ui/status_bar.h"

#include "ui/icons.h"

enum {
    BORDER = 1,
    FILL_INSET = 2,
    NUB_DIVISOR = 5,
    BLUETOOTH_WIDTH_TENTHS = 7,
    BADGE_GAP = 1,
    TENTHS = 10,
    FULL_PERCENT = 100,
    BODY_FIFTHS = 8,
    FIFTHS = 5,
    TINT_STEPS = 3,
    FILL_MIN_WIDTH = 3,
    HALO = 1
};

static int band_top(const Theme *theme, int height)
{
    int band = theme->metrics.status_height > 0 ? theme->metrics.status_height
                                                : theme->metrics.header_height;
    return (band - height) / 2;
}

static int icon_top(const Theme *theme)
{
    return band_top(theme, theme->metrics.status_icon_size);
}

static int nub_width(int size)
{
    return size / NUB_DIVISOR > 0 ? size / NUB_DIVISOR : 1;
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
    return (Rect){playback.x + playback.width + theme->metrics.padding / 2, playback.y,
                  width > 0 ? width : 1, playback.height};
}

Rect status_bar_battery_body(const Theme *theme)
{
    int height = theme->metrics.status_icon_size - 2 * BORDER;
    int width = height * BODY_FIFTHS / FIFTHS;
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
    return (Rect){bluetooth.x + bluetooth.width + theme->metrics.padding / 2, bluetooth.y,
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
    return (Rect){lock.x + lock.width + theme->metrics.padding / 2, lock.y, width, lock.height};
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
    return (StatusBatteryLook){.outline = critical ? danger : text,
                               .fill = state.usb_power ? tint(theme, theme->colors.success)
                                       : low           ? danger
                                                       : text,
                               .bolt = state.charging ? text : theme->colors.header_background,
                               .bolt_halo = state.charging};
}

static void draw_outline(Canvas *canvas, Rect body, Color color)
{
    canvas_fill_rect(canvas, (Rect){body.x, body.y, body.width, BORDER}, color);
    canvas_fill_rect(canvas, (Rect){body.x, body.y + body.height - BORDER, body.width, BORDER},
                     color);
    canvas_fill_rect(canvas, (Rect){body.x, body.y, BORDER, body.height}, color);
    canvas_fill_rect(canvas, (Rect){body.x + body.width - BORDER, body.y, BORDER, body.height},
                     color);
}

static void draw_icon(Canvas *canvas, IconId icon, Rect area, Color color)
{
    icon_draw(canvas, icon, area, (IconInk){color, color});
}

static void draw_bolt(Canvas *canvas, const Theme *theme, Rect body, StatusBatteryLook look)
{
    Rect bolt = {body.x + BORDER, body.y + BORDER, body.width - 2 * BORDER,
                 body.height - 2 * BORDER};
    Canvas inside = canvas_clipped(canvas, bolt);
    for (int dy = -HALO; look.bolt_halo && dy <= HALO; dy++) {
        for (int dx = -HALO; dx <= HALO; dx++) {
            Rect shifted = {bolt.x + dx, bolt.y + dy, bolt.width, bolt.height};
            draw_icon(&inside, ICON_BOLT, shifted, theme->colors.header_background);
        }
    }
    draw_icon(&inside, ICON_BOLT, bolt, look.bolt);
}

static void draw_battery(Canvas *canvas, const Theme *theme, StatusBarState state)
{
    StatusBatteryLook look = status_bar_battery_look(theme, state);
    Rect body = status_bar_battery_body(theme);
    Rect fill = {body.x + FILL_INSET, body.y + FILL_INSET, body.width - 2 * FILL_INSET,
                 body.height - 2 * FILL_INSET};
    fill.width = status_bar_battery_fill_width(state.battery_percent, fill.width);
    int third = body.height / 3;
    draw_outline(canvas, body, look.outline);
    canvas_fill_rect(canvas, fill, look.fill);
    canvas_fill_rect(canvas,
                     (Rect){body.x + body.width, body.y + third, nub_width(body.height),
                            body.height - 2 * third},
                     look.outline);
    if (state.usb_power) {
        draw_bolt(canvas, theme, body, look);
    }
}

static void draw_shuffle(Canvas *canvas, const Theme *theme, StatusShuffle shuffle)
{
    Rect area = status_bar_shuffle_area(theme, shuffle);
    Color color = theme->colors.header_text;
    int arrows = icon_width(ICON_SHUFFLE_SMALL, area.height);
    draw_icon(canvas, shuffle_badge(shuffle),
              (Rect){area.x, area.y, area.width - BADGE_GAP - arrows, area.height}, color);
    draw_icon(canvas, ICON_SHUFFLE_SMALL,
              (Rect){area.x + area.width - arrows, area.y, arrows, area.height}, color);
}

void status_bar_draw(Canvas *canvas, const Theme *theme, StatusBarState state)
{
    Rect icon = status_bar_playback_area(theme);
    Color color = theme->colors.header_text;
    if (state.playback == STATUS_PLAYBACK_PLAYING) {
        draw_icon(canvas, ICON_PLAY, icon, color);
    } else if (state.playback == STATUS_PLAYBACK_PAUSED) {
        draw_icon(canvas, ICON_PAUSE, icon, color);
    }
    if (state.bluetooth != STATUS_BLUETOOTH_OFF) {
        Color tint =
            state.bluetooth == STATUS_BLUETOOTH_CONNECTED ? color : theme->colors.text_tertiary;
        draw_icon(canvas, ICON_BLUETOOTH_SMALL, status_bar_bluetooth_area(theme), tint);
    }
    if (state.locked) {
        draw_icon(canvas, ICON_LOCK, status_bar_lock_area(theme), color);
    }
    if (state.shuffle != STATUS_SHUFFLE_OFF) {
        draw_shuffle(canvas, theme, state.shuffle);
    }
    if (state.battery_known) {
        draw_battery(canvas, theme, state);
    }
}
