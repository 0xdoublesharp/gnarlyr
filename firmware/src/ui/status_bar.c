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
    BADGE_GAP = 4,
    TENTHS = 10,
    FULL_PERCENT = 100,
    BODY_EIGHTHS = 13,
    EIGHTHS = 8,
    TINT_STEPS = 3,
    FILL_MIN_WIDTH = 3,
    HALO = 1,
    SEGMENTS = 5,
    VOLUME_SLOT = 0,
    POSITION_SLOT = 1,
    CLOCK_SLOT = 2,
    REPEAT_SLOT = 3,
    PERCENT_SLOT = 4,
    LOGO_SLOT = 5,
    PLAYBACK_SLOT = 6,
    BLUETOOTH_SLOT = 7,
    LOCK_SLOT = 8,
    SHUFFLE_SLOT = 9
};

static const uint8_t PORTRAIT_ORDER[] = {CLOCK_SLOT,   VOLUME_SLOT,   POSITION_SLOT,  REPEAT_SLOT,
                                         PERCENT_SLOT, PLAYBACK_SLOT, BLUETOOTH_SLOT, SHUFFLE_SLOT};
static const uint8_t LANDSCAPE_ORDER[] = {VOLUME_SLOT,    POSITION_SLOT, CLOCK_SLOT,
                                          REPEAT_SLOT,    PERCENT_SLOT,  PLAYBACK_SLOT,
                                          BLUETOOTH_SLOT, SHUFFLE_SLOT};

static int band_top(const Theme *theme, int height)
{
    return (theme->metrics.header_height - height) / 2;
}

static int icon_top(const Theme *theme)
{
    return band_top(theme, theme->metrics.status_icon_size);
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

static IconId shuffle_badge(StatusShuffle shuffle)
{
    return shuffle == STATUS_SHUFFLE_ALBUMS ? ICON_ALBUM_SMALL : ICON_SONG_SMALL;
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

static int slot_width(const Theme *theme, const StatusBarState *state, int slot)
{
    int icon = theme->metrics.status_icon_size;
    int narrow = icon * BLUETOOTH_WIDTH_TENTHS / TENTHS;
    const char *text = slot == VOLUME_SLOT     ? state->volume
                       : slot == POSITION_SLOT ? state->position
                       : slot == CLOCK_SLOT    ? state->clock
                       : slot == PERCENT_SLOT && !theme->status_bar_compact && state->battery_known
                           ? state->percent
                           : NULL;
    int width = text != NULL ? text_width(theme->fonts.small, text) : 0;
    switch (slot) {
    case REPEAT_SLOT:
        return state->repeat == 0
                   ? 0
                   : icon +
                         (state->repeat == 1 ? BADGE_GAP + icon_width(ICON_SONG_SMALL, icon) : 0);
    case LOGO_SLOT:
        return state->logo ? icon : 0;
    case PLAYBACK_SLOT:
        return state->playback != STATUS_PLAYBACK_NONE ? icon : 0;
    case BLUETOOTH_SLOT:
        return state->bluetooth != STATUS_BLUETOOTH_OFF ? narrow : 0;
    case LOCK_SLOT:
        return state->locked ? narrow : 0;
    case SHUFFLE_SLOT:
        return state->shuffle == STATUS_SHUFFLE_OFF
                   ? 0
                   : icon_width(shuffle_badge(state->shuffle), icon) + BADGE_GAP +
                         icon_width(ICON_SHUFFLE_SMALL, icon);
    default:
        return width + (slot == VOLUME_SLOT && width > 0 ? icon + BADGE_GAP : 0);
    }
}

int status_bar_left_end(const Theme *theme, StatusBarState state)
{
    int end = theme->metrics.padding;
    for (int slot = LOGO_SLOT; slot < STATUS_BAR_SLOTS; slot++) {
        int width = slot_width(theme, &state, slot);
        end += width > 0 ? width + (end > theme->metrics.padding ? theme->metrics.padding : 0) : 0;
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

typedef struct StatusFit {
    const Theme *theme;
    int start;
    int end;
    int icons;
    int leading;
    int trailing;
} StatusFit;

typedef struct TitleRoom {
    int left;
    int right;
    int shown;
} TitleRoom;

static int right_end(const Theme *theme, const StatusBarState *state)
{
    return state->battery_known ? status_bar_battery_body(theme).x
                                : theme->metrics.screen_width - (theme->metrics.padding >> 1);
}

static TitleRoom title_room(const Theme *theme, const StatusBarState *state)
{
    int pad = theme->metrics.padding;
    int logo = slot_width(theme, state, LOGO_SLOT);
    int lock = slot_width(theme, state, LOCK_SLOT);
    int left = pad + (logo > 0 ? logo + pad : 0);
    int right = right_end(theme, state) - pad - (lock > 0 ? lock + pad : 0);
    int shown = state->title_width < right - left ? state->title_width : right - left;
    return (TitleRoom){left, right, shown > 0 ? shown : 0};
}

Rect status_bar_title_area(const Theme *theme, const StatusBarState *state)
{
    TitleRoom room = title_room(theme, state);
    return (Rect){room.left, 0, room.right - room.left, theme->metrics.header_height};
}

static bool fit_room(const StatusFit *fit)
{
    int pad = fit->theme->metrics.padding;
    return fit->start + fit->icons + fit->leading + pad <= fit->end - fit->trailing;
}

static bool trailing_slot(int slot)
{
    return slot == CLOCK_SLOT || slot == PERCENT_SLOT || slot == POSITION_SLOT;
}

static bool fit_add(StatusFit *fit, int slot, int width, bool forced)
{
    StatusFit next = *fit;
    int pad = fit->theme->metrics.padding;
    if (slot > LOGO_SLOT) {
        next.icons += width + pad;
    } else if (trailing_slot(slot)) {
        next.trailing += width + (pad >> 1);
    } else {
        next.leading += width + (pad >> 1);
    }
    if (!forced && !fit_room(&next)) {
        return false;
    }
    *fit = next;
    return true;
}

static void place_slot(const Theme *theme, StatusBarLayout *layout, int slot, int x)
{
    Rect body = status_bar_battery_body(theme);
    int icon = theme->metrics.status_icon_size;
    Rect *area = &layout->areas[slot];
    *area = slot >= LOGO_SLOT ? (Rect){x, icon_top(theme), area->width, icon}
                              : (Rect){x, body.y, area->width, body.height};
}

static int place_left(const StatusFit *fit, StatusBarLayout *layout)
{
    static const uint8_t LEADING[] = {REPEAT_SLOT, VOLUME_SLOT};
    const Theme *theme = fit->theme;
    int pad = theme->metrics.padding;
    int left = fit->start;
    for (int slot = LOGO_SLOT + 1; slot < STATUS_BAR_SLOTS; slot++) {
        if ((layout->items & (1u << slot)) != 0) {
            place_slot(theme, layout, slot, left);
            left += layout->areas[slot].width + pad;
        }
    }
    left -= left > fit->start ? pad >> 1 : 0;
    for (int index = 0; index < 2; index++) {
        int slot = LEADING[index];
        if ((layout->items & (1u << slot)) != 0) {
            place_slot(theme, layout, slot, left);
            left += (pad >> 1) + layout->areas[slot].width;
        }
    }
    return left;
}

static void place(const StatusFit *fit, StatusBarLayout *layout)
{
    static const uint8_t TRAILING[] = {PERCENT_SLOT, CLOCK_SLOT, POSITION_SLOT};
    const Theme *theme = fit->theme;
    int pad = theme->metrics.padding;
    if ((layout->items & (1u << LOGO_SLOT)) != 0) {
        place_slot(theme, layout, LOGO_SLOT, pad);
    }
    place_left(fit, layout);
    int right = fit->end;
    for (int index = 0; index < 3; index++) {
        int slot = TRAILING[index];
        if ((layout->items & (1u << slot)) != 0) {
            right -= (pad >> 1) + layout->areas[slot].width;
            place_slot(theme, layout, slot, right);
        }
    }
}

static void fit_slot(StatusFit *fit, StatusBarLayout *layout, int slot, bool forced)
{
    int width = layout->areas[slot].width;
    if (width > 0 && (layout->items & (1u << slot)) == 0 && fit_add(fit, slot, width, forced)) {
        layout->items |= (uint16_t)(1u << slot);
    }
}

static int cluster_start(const Theme *theme, const StatusBarState *state)
{
    TitleRoom room = title_room(theme, state);
    return room.left + room.shown + (state->title_width > 0 ? theme->metrics.padding : 0);
}

CORE_SEAM void status_bar_layout(const Theme *theme, const StatusBarState *state,
                                 StatusBarLayout *layout)
{
    StatusFit fit = {
        .theme = theme, .start = cluster_start(theme, state), .end = right_end(theme, state)};
    layout->items = 0;
    for (int slot = 0; slot < STATUS_BAR_SLOTS; slot++) {
        layout->areas[slot] = (Rect){0, 0, slot_width(theme, state, slot), 0};
    }
    if (layout->areas[LOGO_SLOT].width > 0) {
        layout->items |= STATUS_BAR_LOGO;
    }
    fit_slot(&fit, layout, LOCK_SLOT, true);
    const uint8_t *order = theme_is_portrait(theme) ? PORTRAIT_ORDER : LANDSCAPE_ORDER;
    for (int index = 0; index < (int)sizeof PORTRAIT_ORDER; index++) {
        fit_slot(&fit, layout, order[index], false);
    }
    place(&fit, layout);
}

uint16_t status_bar_items(const Theme *theme, const StatusBarState *state)
{
    StatusBarLayout layout;
    status_bar_layout(theme, state, &layout);
    return layout.items;
}

Rect status_bar_item_area(const Theme *theme, const StatusBarState *state, uint16_t item)
{
    StatusBarLayout layout;
    status_bar_layout(theme, state, &layout);
    int index = 0;
    while ((item >>= 1) != 0) {
        index++;
    }
    return layout.areas[index];
}

static void draw_shuffle(Canvas *canvas, const Theme *theme, StatusShuffle shuffle, Rect area)
{
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

static void draw_status_icon(Canvas *canvas, const Theme *theme, const StatusBarState *state,
                             int slot, Rect area)
{
    Color color = theme->colors.header_text;
    if (slot == SHUFFLE_SLOT) {
        draw_shuffle(canvas, theme, state->shuffle, area);
        return;
    }
    if (slot == BLUETOOTH_SLOT && state->bluetooth != STATUS_BLUETOOTH_CONNECTED) {
        color = theme->colors.text_tertiary;
    }
    IconId icon = slot == LOGO_SLOT                            ? ICON_LOGO
                  : slot == BLUETOOTH_SLOT                     ? ICON_BLUETOOTH_SMALL
                  : slot == LOCK_SLOT                          ? ICON_LOCK
                  : state->playback == STATUS_PLAYBACK_PLAYING ? ICON_PLAY
                                                               : ICON_PAUSE;
    draw_icon(canvas, theme, (StatusIcon){icon, area, color});
}

static void draw_text_slot(Canvas *canvas, const Theme *theme, const StatusBarState *state,
                           int slot, Rect area)
{
    if (slot == REPEAT_SLOT || slot == VOLUME_SLOT) {
        Rect icon = {area.x, icon_top(theme), theme->metrics.status_icon_size,
                     theme->metrics.status_icon_size};
        draw_icon(canvas, theme,
                  (StatusIcon){slot == REPEAT_SLOT ? ICON_REPEAT : ICON_SPEAKER, icon,
                               theme->colors.header_text});
        area.x += icon.width + BADGE_GAP;
        if (slot == REPEAT_SLOT && state->repeat == 1) {
            icon.x = area.x;
            icon.width = icon_width(ICON_SONG_SMALL, icon.height);
            draw_icon(canvas, theme,
                      (StatusIcon){ICON_SONG_SMALL, icon, theme->colors.header_text});
        }
    }
    TextStyle style = {theme->fonts.small, theme->colors.header_text, false};
    if (slot == PERCENT_SLOT && !state->usb_power &&
        state->battery_percent <= STATUS_BAR_LOW_BATTERY_PERCENT) {
        style.color = tint(theme, theme->colors.danger);
    }
    const char *text = slot == VOLUME_SLOT     ? state->volume
                       : slot == POSITION_SLOT ? state->position
                       : slot == CLOCK_SLOT    ? state->clock
                       : slot == PERCENT_SLOT  ? state->percent
                                               : "";
    text_draw(canvas, style, (Point){area.x, label_baseline(style.font, area)}, text);
}

CORE_SEAM void status_bar_draw(Canvas *canvas, const Theme *theme, StatusBarState state)
{
    StatusBarLayout layout;
    status_bar_layout(theme, &state, &layout);
    if (state.battery_known) {
        draw_battery(canvas, theme, &state);
    }
    for (int slot = 0; slot < STATUS_BAR_SLOTS; slot++) {
        if ((layout.items & (1u << slot)) == 0) {
            continue;
        }
        if (slot >= LOGO_SLOT) {
            draw_status_icon(canvas, theme, &state, slot, layout.areas[slot]);
        } else {
            draw_text_slot(canvas, theme, &state, slot, layout.areas[slot]);
        }
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
