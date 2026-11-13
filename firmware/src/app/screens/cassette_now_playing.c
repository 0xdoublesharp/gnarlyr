#include "cassette_now_playing.h"

#include "core/i18n.h"
#include "core/seam.h"
#include "gfx/strip_renderer.h"
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/display.h"
#include "home_art.h"
#include "neon_art.h"
#include "ui/icons.h"
#include "ui/label.h"

enum {
    PHASE_STEPS = 16,
    SPOKES = 3,
    SINE_SCALE = 16
};

static const int8_t SINE[PHASE_STEPS] = {0, 6,  11,  15,  16,  15,  11,  6,
                                         0, -6, -11, -15, -16, -15, -11, -6};

CassetteNowPlayingLayout cassette_now_playing_layout(const Theme *theme)
{
    bool portrait = theme_is_portrait(theme);
    int padding = theme->metrics.padding;
    Rect body = {padding, portrait ? 74 : 34, theme->metrics.screen_width - padding * 2,
                 portrait ? 244 : 156};
    int left = body.x + 18;
    int width = body.width - 36;
    int top = body.y + 6;
    int title = font_line_height(theme->fonts.title);
    int artist = font_line_height(theme->fonts.body);
    int small = font_line_height(theme->fonts.small);
    int reel_size = portrait ? 48 : 38;
    int reel_y = portrait ? body.y + 140 : body.y + 100;
    int gap = portrait ? 28 : 100;
    int center = body.x + body.width / 2;
    Rect first = {center - gap / 2 - reel_size, reel_y, reel_size, reel_size};
    Rect second = {center + gap / 2, reel_y, reel_size, reel_size};
    return (CassetteNowPlayingLayout){.body = body,
                                      .label = {{left, top, width, title},
                                                {left, top + title, width, artist},
                                                {left, top + title + artist, width, small}},
                                      .reels = {first, second},
                                      .window = {first.x - 6, first.y - 2,
                                                 second.x + second.width - first.x + 12,
                                                 reel_size + 4}};
}

int cassette_now_playing_radius(uint32_t position_ms, uint32_t duration_ms, int maximum)
{
    int minimum = maximum / 3;
    if (duration_ms == 0) {
        return minimum;
    }
    uint32_t played = position_ms < duration_ms ? position_ms : duration_ms;
    int area =
        minimum * minimum +
        (int)((uint64_t)played * (unsigned)(maximum * maximum - minimum * minimum) / duration_ms);
    int radius = minimum;
    while ((radius + 1) * (radius + 1) <= area && radius < maximum) {
        radius++;
    }
    return radius;
}

static void disc(Canvas *canvas, Rect area, Color color)
{
    icon_draw(canvas, ICON_DISC, area, (IconInk){color, color});
}

static uint32_t tape_position(const NowPlayingScreen *screen)
{
    if (screen->scrubbing) {
        return screen->scrub_position_ms;
    }
    return screen->status.state == PLAYER_PLAYING ? screen->reel_position_ms
                                                  : screen->status.position_ms;
}

static void ring(Canvas *canvas, Rect area, const Theme *theme, Color color)
{
    int radius = area.width / 2 - 2;
    Point center = {area.x + area.width / 2, area.y + area.height / 2};
    for (int step = 0; step < PHASE_STEPS; step++) {
        int next = (step + 1) % PHASE_STEPS;
        Point from = {center.x + SINE[step] * radius / SINE_SCALE,
                      center.y + SINE[(step + 4) % PHASE_STEPS] * radius / SINE_SCALE};
        Point to = {center.x + SINE[next] * radius / SINE_SCALE,
                    center.y + SINE[(next + 4) % PHASE_STEPS] * radius / SINE_SCALE};
        neon_art_line(canvas, from, to, neon_art_ink(theme, color));
    }
}

static void hub(Canvas *canvas, Rect reel, const NowPlayingScreen *screen)
{
    const Theme *theme = screen->config.theme;
    int cx = reel.x + reel.width / 2;
    int cy = reel.y + reel.height / 2;
    int phase = (int)(tape_position(screen) / 60 % PHASE_STEPS);
    for (int spoke = 0; spoke < SPOKES; spoke++) {
        int angle = (phase + spoke * PHASE_STEPS / SPOKES) % PHASE_STEPS;
        int radius = reel.width / 2 - 5;
        Point end = {cx + SINE[angle] * radius / SINE_SCALE,
                     cy + SINE[(angle + PHASE_STEPS / 4) % PHASE_STEPS] * radius / SINE_SCALE};
        neon_art_line(canvas, (Point){cx, cy}, end, neon_art_ink(theme, theme->colors.accent));
    }
    disc(canvas, (Rect){cx - 2, cy - 2, 4, 4}, theme->colors.text);
}

static void draw_reels(const NowPlayingScreen *screen, Canvas *canvas)
{
    const Theme *theme = screen->config.theme;
    CassetteNowPlayingLayout layout = cassette_now_playing_layout(theme);
    uint32_t position = tape_position(screen);
    uint32_t duration = screen->status.duration_ms;
    if (position > duration) {
        position = duration;
    }
    for (int index = 0; index < 2; index++) {
        Rect reel = layout.reels[index];
        if (rect_is_empty(rect_intersection(canvas->clip, reel))) {
            continue;
        }
        Canvas clipped = canvas_clipped(canvas, reel);
        canvas_fill_rect(&clipped, reel, theme->colors.backdrop_top);
        int radius = cassette_now_playing_radius(index == 0 ? duration - position : position,
                                                 duration, reel.width / 2);
        Rect pack = {reel.x + reel.width / 2 - radius, reel.y + reel.height / 2 - radius,
                     radius * 2, radius * 2};
        ring(&clipped, reel, theme, theme->colors.accent_alt);
        ring(&clipped, pack, theme, theme->colors.text_tertiary);
        hub(&clipped, reel, screen);
    }
}

static void label_line(Canvas *canvas, Label label, TextStyle style)
{
    label_draw(canvas, style, label);
}

static void draw_shell(Canvas *canvas, CassetteNowPlayingLayout layout, const Theme *theme)
{
    Rect body = layout.body;
    home_art_draw_backdrop(canvas, body, theme);
    Rect rim = {body.x + 2, body.y + 2, body.width - 4, body.height - 4};
    neon_art_frame(canvas, rim, neon_art_ink(theme, theme->colors.accent_alt));
    int label_bottom = layout.reels[0].y - 10;
    neon_art_line(canvas, (Point){body.x + 10, label_bottom},
                  (Point){body.x + body.width - 10, label_bottom},
                  neon_art_ink(theme, theme->colors.accent));
    canvas_fill_rect(canvas, layout.window, theme->colors.backdrop_top);
    neon_art_frame(canvas, layout.window, neon_art_ink(theme, theme->colors.accent));
    int y = body.y + body.height - 18;
    Point left = {body.x + body.width / 4, y};
    Point right = {body.x + body.width * 3 / 4, y};
    NeonInk ink = neon_art_ink(theme, theme->colors.accent_alt);
    neon_art_line(canvas, left, right, ink);
    neon_art_line(canvas, left, (Point){left.x - 8, y + 14}, ink);
    neon_art_line(canvas, right, (Point){right.x + 8, y + 14}, ink);
    for (int corner = 0; corner < 4; corner++) {
        int x = body.x + ((corner & 1) != 0 ? body.width - 8 : 4);
        int sy = body.y + ((corner & 2) != 0 ? body.height - 8 : 4);
        disc(canvas, (Rect){x, sy, 4, 4}, theme->colors.accent_alt);
    }
}

void cassette_now_playing_draw(const NowPlayingScreen *screen, Canvas *canvas)
{
    const Theme *theme = screen->config.theme;
    CassetteNowPlayingLayout layout = cassette_now_playing_layout(theme);
    if (rect_is_empty(rect_intersection(canvas->clip, layout.body))) {
        return;
    }
    draw_shell(canvas, layout, theme);
    label_line(canvas, (Label){layout.label[0], screen->title},
               (TextStyle){theme->fonts.title, theme->colors.text, false});
    label_line(canvas, (Label){layout.label[1], screen->artist},
               (TextStyle){theme->fonts.body, theme->colors.accent, false});
    label_line(canvas, (Label){layout.label[2], screen->album},
               (TextStyle){theme->fonts.small, theme->colors.text_secondary, false});
    int y = layout.body.y + layout.body.height - 20;
    TextStyle footer = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    label_draw(canvas, footer,
               (Label){{layout.body.x + 12, y, 40, 18}, i18n_text(STR_CASSETTE_C60)});
    if (screen->tape_side == 1 || screen->tape_side == 2) {
        StringId side = screen->tape_side == 1 ? STR_CASSETTE_SIDE_A : STR_CASSETTE_SIDE_B;
        label_draw(canvas, footer,
                   (Label){{layout.body.x + layout.body.width - 30, y, 18, 18}, i18n_text(side)});
    }
    if (screen->scrubbing) {
        Rect head = {layout.body.x + layout.body.width / 4, y, layout.body.width / 2, 18};
        label_line(canvas, (Label){head, i18n_text(STR_SEEKING)},
                   (TextStyle){theme->fonts.small, theme->colors.text, false});
    }
    draw_reels(screen, canvas);
}

static void draw_motion(void *context, Canvas *canvas)
{
    draw_reels(context, canvas);
}

static void send_reels(void *context, Rect area, const Color *pixels)
{
    (void)context;
    hal_audio_service();
    hal_bluetooth_service();
    hal_display_write(area.x, area.y, area.width, area.height, pixels);
}

CORE_SEAM bool cassette_now_playing_animate(NowPlayingScreen *screen, FrameDiff *diff, bool visible)
{
    bool playing = visible && screen->status.has_track && screen->status.state == PLAYER_PLAYING &&
                   !screen->scrubbing &&
                   !now_playing_feedback_visible(screen->feedback, screen->feedback_started_ms,
                                                 screen->now_ms);
    CassetteNowPlayingLayout layout = cassette_now_playing_layout(screen->config.theme);
    Rect mask = playing ? layout.window : (Rect){0};
    bool changed = mask.x != diff->masked.x || mask.y != diff->masked.y ||
                   mask.width != diff->masked.width || mask.height != diff->masked.height;
    if (changed) {
        frame_diff_mask(diff, mask);
    }
    uint32_t frame = screen->now_ms / CASSETTE_NOW_PLAYING_FRAME_MS;
    if (!playing || frame == screen->reel_frame) {
        return changed;
    }
    screen->reel_frame = frame;
    screen->reel_position_ms = screen->status.position_ms;
    for (int index = 0; index < 2; index++) {
        Rect reel = layout.reels[index];
        neon_art_render(reel, screen->config.theme->colors.background,
                        (FrameDrawer){draw_motion, screen}, (FrameSink){send_reels, NULL});
    }
    hal_display_present();
    return changed;
}
