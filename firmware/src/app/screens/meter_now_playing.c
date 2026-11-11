#include "meter_now_playing.h"

#include "core/i18n.h"
#include "core/seam.h"
#include "gfx/strip_renderer.h"
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/display.h"
#include "home_art.h"
#include "neon_art.h"
#include "ui/label.h"

MeterNowPlayingLayout meter_now_playing_layout(const Theme *theme)
{
    bool portrait = theme_is_portrait(theme);
    int padding = theme->metrics.padding;
    int width = theme->metrics.screen_width - padding * 2;
    int title = font_line_height(theme->fonts.title);
    int artist = font_line_height(theme->fonts.body);
    if (portrait) {
        return (MeterNowPlayingLayout){
            .stage = {padding, 64, width, 214},
            .dials = {{padding + 4, 76, width - 8, 86}, {padding + 4, 180, width - 8, 86}},
            .spectrum = {padding + 4, 88, width - 8, 180},
            .text = {{padding, 292, width, title}, {padding, 298 + title, width, artist}}};
    }
    return (MeterNowPlayingLayout){
        .stage = {padding, 38, 278, 150},
        .dials = {{padding + 4, 68, 128, 92}, {padding + 146, 68, 128, 92}},
        .spectrum = {padding + 4, 60, 270, 116},
        .text = {{padding + 294, 48, width - 294, title},
                 {padding + 294, 56 + title, width - 294, artist}}};
}

typedef struct MeterState {
    AudioVisualWork work;
    AudioVisualNeedle needles[2];
    uint8_t shown[2];
    uint8_t echo[2];
    uint32_t peak_until[2];
    uint8_t bars[AUDIO_VISUAL_BANDS];
    uint8_t caps[AUDIO_VISUAL_BANDS];
    uint32_t cap_until[AUDIO_VISUAL_BANDS];
    uint32_t frame;
    uint32_t updated_ms;
    uint32_t text_changed_ms;
    uint16_t track;
    NowPlayingStyle style;
    bool active;
} MeterState;

static MeterState state;
static Color strip_pixels[1024];

void meter_now_playing_reset(void)
{
    state = (MeterState){.frame = UINT32_MAX};
}

static Point pivot(Rect area)
{
    return (Point){area.x + area.width / 2, area.y + area.height - 8};
}

static Point tip(Rect area, uint8_t level)
{
    static const int8_t X[] = {-87, -71, -50, -26, 0, 26, 50, 71, 87};
    static const int8_t Y[] = {-50, -71, -87, -97, -100, -97, -87, -71, -50};
    unsigned phase = (unsigned)level * 8;
    unsigned index = phase / 256;
    unsigned fraction = phase % 256;
    int x = X[index] + (X[index + 1] - X[index]) * (int)fraction / 256;
    int y = Y[index] + (Y[index + 1] - Y[index]) * (int)fraction / 256;
    int radius = (area.width - 16) / 2;
    radius = radius < area.height - 20 ? radius : area.height - 20;
    Point center = pivot(area);
    return (Point){center.x + x * radius / 100, center.y + y * radius / 100};
}

static Rect peak_dot(Rect area)
{
    return (Rect){area.x + area.width - 10, area.y + 4, 6, 6};
}

static void dial(const NowPlayingScreen *screen, Canvas *canvas, int channel)
{
    const Theme *theme = screen->config.theme;
    Rect area = meter_now_playing_layout(theme).dials[channel];
    Canvas clipped = canvas_clipped(canvas, area);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    NeonInk scale = neon_art_ink(theme, theme->colors.accent_alt);
    for (int step = 0; step < 16; step++) {
        uint8_t from = (uint8_t)(step * 255 / 16);
        uint8_t to = (uint8_t)((step + 1) * 255 / 16);
        neon_art_line(&clipped, tip(area, from), tip(area, to), scale);
    }
    Point center = pivot(area);
    for (int tick = 0; tick < 9; tick++) {
        Point outer = tip(area, (uint8_t)(tick * 255 / 8));
        Point inner = {center.x + (outer.x - center.x) * 7 / 8,
                       center.y + (outer.y - center.y) * 7 / 8};
        neon_art_line(&clipped, outer, inner, scale);
    }
    Color echo = color_between(theme->colors.backdrop_top, theme->colors.accent, 1, 3);
    neon_art_line(&clipped, center, tip(area, state.echo[channel]), neon_art_ink(theme, echo));
    neon_art_line(&clipped, center, tip(area, state.shown[channel]),
                  neon_art_ink(theme, theme->colors.accent));
    canvas_fill_rect(&clipped, (Rect){center.x - 2, center.y - 2, 5, 5}, theme->colors.text);
    Rect dot = peak_dot(area);
    Color led = (int32_t)(state.peak_until[channel] - screen->now_ms) > 0 ? theme->colors.danger
                                                                          : theme->colors.divider;
    canvas_fill_rect(&clipped, dot, color_between(theme->colors.backdrop_top, led, 1, 3));
    canvas_fill_rect(&clipped, (Rect){dot.x + 1, dot.y + 1, 4, 4}, led);
    TextStyle label = {theme->fonts.small, theme->colors.text_secondary};
    label_draw(&clipped, label,
               (Label){{area.x + 2, area.y + 2, 32, font_line_height(label.font)},
                       i18n_text(channel == 0 ? STR_METER_LEFT : STR_METER_RIGHT)});
}

static void spectrum(const NowPlayingScreen *screen, Canvas *canvas)
{
    const Theme *theme = screen->config.theme;
    Rect area = meter_now_playing_layout(theme).spectrum;
    Canvas clipped = canvas_clipped(canvas, area);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    int pitch = area.width / AUDIO_VISUAL_BANDS;
    int width = pitch - (pitch > 10 ? 4 : 2);
    int bottom = area.y + area.height - 5;
    int height = area.height - 16;
    for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
        int x = area.x + band * pitch + (area.width - pitch * AUDIO_VISUAL_BANDS) / 2;
        int rise = state.bars[band] * height / 255;
        Color ink = color_between(theme->colors.accent, theme->colors.accent_alt, band,
                                  AUDIO_VISUAL_BANDS - 1);
        Rect bar = {x, bottom - rise, width, rise};
        canvas_fill_gradient(&clipped, bar, ink,
                             color_between(theme->colors.backdrop_bottom, ink, 1, 4));
        if (rise > 0) {
            neon_art_line(&clipped, (Point){x, bottom - rise}, (Point){x, bottom - 1},
                          neon_art_ink(theme, ink));
            for (int y = bottom - rise + 3; y < bottom; y += 4) {
                canvas_fill_rect(&clipped, (Rect){x + 2, y, width - 2, 1},
                                 theme->colors.backdrop_top);
            }
        }
        int cap = bottom - state.caps[band] * height / 255;
        canvas_fill_rect(&clipped, (Rect){x, cap - 2, width, 2}, theme->colors.text);
    }
}

static void drawing(const NowPlayingScreen *screen, Canvas *canvas)
{
    const Theme *theme = screen->config.theme;
    MeterNowPlayingLayout layout = meter_now_playing_layout(theme);
    Canvas clipped = canvas_clipped(canvas, layout.stage);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    home_art_draw_backdrop(&clipped, layout.stage, theme);
    if (settings_now_playing_style(screen->config.settings) == NOW_PLAYING_VU) {
        dial(screen, &clipped, 0);
        dial(screen, &clipped, 1);
    } else {
        spectrum(screen, &clipped);
    }
}

void meter_now_playing_draw(const NowPlayingScreen *screen, Canvas *canvas)
{
    const Theme *theme = screen->config.theme;
    MeterNowPlayingLayout layout = meter_now_playing_layout(theme);
    drawing(screen, canvas);
    label_draw(canvas, (TextStyle){theme->fonts.title, theme->colors.text},
               (Label){layout.text[0], screen->title});
    label_draw(canvas, (TextStyle){theme->fonts.body, theme->colors.text_secondary},
               (Label){layout.text[1], screen->artist});
}

static void draw_motion(void *context, Canvas *canvas)
{
    drawing(context, canvas);
}

static void send_motion(void *context, Rect area, const Color *pixels)
{
    (void)context;
    hal_audio_service();
    hal_bluetooth_service();
    hal_display_write(area.x, area.y, area.width, area.height, pixels);
}

static void render(NowPlayingScreen *screen, Rect area)
{
    StripRenderer renderer = {.strip_pixels = strip_pixels,
                              .area = area,
                              .strip_height = 1024 / area.width,
                              .background = screen->config.theme->colors.background};
    strip_renderer_render(&renderer, (FrameDrawer){draw_motion, screen},
                          (FrameSink){send_motion, NULL});
}

static uint8_t falling(uint8_t value, unsigned fall)
{
    return value > fall ? (uint8_t)(value - fall) : 0;
}

static void update_bars(const AudioVisualLevels *levels, uint32_t elapsed, uint32_t now)
{
    for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
        uint8_t next = levels->band[band];
        uint8_t lower = falling(state.bars[band], elapsed / 4);
        state.bars[band] = next > lower ? next : lower;
        if (next >= state.caps[band] && next > 0) {
            state.caps[band] = next;
            state.cap_until[band] = now + 400;
        } else if ((int32_t)(now - state.cap_until[band]) >= 0) {
            state.caps[band] = falling(state.caps[band], elapsed / 12);
        }
    }
}

static Rect needle_area(Rect dial, uint8_t previous, uint8_t current)
{
    Point center = pivot(dial);
    Point first = tip(dial, previous);
    Point second = tip(dial, current);
    int left = first.x < second.x ? first.x : second.x;
    int right = first.x > second.x ? first.x : second.x;
    int top = first.y < second.y ? first.y : second.y;
    left = left < center.x ? left : center.x;
    right = right > center.x ? right : center.x;
    return rect_intersection(dial, (Rect){left - 2, top - 2, right - left + 5, center.y - top + 5});
}

static Rect joined(Rect first, Rect second)
{
    int x = first.x < second.x ? first.x : second.x;
    int y = first.y < second.y ? first.y : second.y;
    int right = first.x + first.width > second.x + second.width ? first.x + first.width
                                                                : second.x + second.width;
    int bottom = first.y + first.height > second.y + second.height ? first.y + first.height
                                                                   : second.y + second.height;
    return (Rect){x, y, right - x, bottom - y};
}

static void update_needles(NowPlayingScreen *screen, const AudioVisualLevels *levels,
                           uint32_t elapsed)
{
    MeterNowPlayingLayout layout = meter_now_playing_layout(screen->config.theme);
    for (int channel = 0; channel < 2; channel++) {
        uint8_t previous_echo = state.echo[channel];
        state.echo[channel] = state.shown[channel];
        audio_visual_needle(&state.needles[channel], levels->mean[channel], elapsed);
        state.shown[channel] = audio_visual_db((uint32_t)state.needles[channel].position / 256);
        if (previous_echo != state.echo[channel] || state.shown[channel] != state.echo[channel]) {
            Rect area = joined(
                needle_area(layout.dials[channel], previous_echo, state.echo[channel]),
                needle_area(layout.dials[channel], state.echo[channel], state.shown[channel]));
            render(screen, area);
        }
        bool was_lit = (int32_t)(state.peak_until[channel] - state.updated_ms) > 0;
        if (levels->peak[channel] >= 30000) {
            state.peak_until[channel] = screen->now_ms + 500;
        }
        bool lit = (int32_t)(state.peak_until[channel] - screen->now_ms) > 0;
        if (lit != was_lit) {
            render(screen, peak_dot(layout.dials[channel]));
        }
    }
}

CORE_SEAM bool meter_now_playing_animate(NowPlayingScreen *screen, FrameDiff *diff, bool visible)
{
    bool active = visible && screen->status.has_track && !screen->scrubbing &&
                  !now_playing_feedback_visible(screen->feedback, screen->feedback_started_ms,
                                                screen->now_ms);
    if (!active) {
        state.active = false;
        bool changed = !rect_is_empty(diff->masked);
        if (changed) {
            frame_diff_mask(diff, (Rect){0});
        }
        return changed;
    }
    NowPlayingStyle style = settings_now_playing_style(screen->config.settings);
    if (state.style != style || state.track != screen->status.track ||
        state.text_changed_ms != screen->text_changed_ms) {
        meter_now_playing_reset();
        state.style = style;
        state.track = screen->status.track;
        state.text_changed_ms = screen->text_changed_ms;
    }
    Rect mask = meter_now_playing_layout(screen->config.theme).stage;
    bool changed = diff->masked.x != mask.x || diff->masked.y != mask.y ||
                   diff->masked.width != mask.width || diff->masked.height != mask.height;
    if (changed) {
        frame_diff_mask(diff, mask);
    }
    uint32_t frame = screen->now_ms / AUDIO_VISUAL_FRAME_MS;
    if (state.active && state.frame == frame) {
        return changed;
    }
    uint32_t elapsed = state.active ? screen->now_ms - state.updated_ms : AUDIO_VISUAL_FRAME_MS;
    elapsed = elapsed > 240 ? 240 : elapsed;
    state.active = true;
    state.frame = frame;
    AudioVisualLevels levels = {0};
    if (screen->status.state == PLAYER_PLAYING) {
        levels = audio_visual_measure(&state.work, hal_audio_visual_buffer(),
                                      style == NOW_PLAYING_SPECTRUM);
    }
    if (style == NOW_PLAYING_VU) {
        update_needles(screen, &levels, elapsed);
    } else {
        update_bars(&levels, elapsed, screen->now_ms);
        render(screen, meter_now_playing_layout(screen->config.theme).spectrum);
    }
    state.updated_ms = screen->now_ms;
    hal_display_present();
    return changed;
}
