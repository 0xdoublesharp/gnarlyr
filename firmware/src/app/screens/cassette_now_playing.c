#include "cassette_now_playing.h"

#include "core/i18n.h"
#include "core/seam.h"
#include "gfx/strip_renderer.h"
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/display.h"
#include "ui/icons.h"
#include "ui/label.h"

enum {
    REEL_PIXELS = 1024,
    PHASE_STEPS = 16,
    SPOKES = 3,
    SINE_SCALE = 16
};

static Color reel_pixels[REEL_PIXELS];
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

static void hub(Canvas *canvas, Rect reel, const NowPlayingScreen *screen)
{
    const Theme *theme = screen->config.theme;
    int cx = reel.x + reel.width / 2;
    int cy = reel.y + reel.height / 2;
    int phase = (int)(tape_position(screen) / 60 % PHASE_STEPS);
    disc(canvas, (Rect){cx - 8, cy - 8, 16, 16}, theme->colors.accent);
    for (int spoke = 0; spoke < SPOKES; spoke++) {
        int angle = (phase + spoke * PHASE_STEPS / SPOKES) % PHASE_STEPS;
        int x = cx + SINE[angle] * 5 / SINE_SCALE;
        int y = cy + SINE[(angle + PHASE_STEPS / 4) % PHASE_STEPS] * 5 / SINE_SCALE;
        disc(canvas, (Rect){x - 2, y - 2, 4, 4}, theme->colors.background);
    }
    disc(canvas, (Rect){cx - 2, cy - 2, 4, 4}, theme->colors.background);
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
        canvas_fill_rect(&clipped, reel, theme->colors.background);
        int radius = cassette_now_playing_radius(index == 0 ? duration - position : position,
                                                 duration, reel.width / 2);
        Rect pack = {reel.x + reel.width / 2 - radius, reel.y + reel.height / 2 - radius,
                     radius * 2, radius * 2};
        disc(&clipped, pack, theme->colors.text_tertiary);
        hub(&clipped, reel, screen);
    }
}

static void label_line(Canvas *canvas, const Theme *theme, Label label, const Font *font)
{
    label_draw(canvas, (TextStyle){font, theme->colors.text}, label);
}

static void draw_shell(Canvas *canvas, CassetteNowPlayingLayout layout, const Theme *theme)
{
    Rect body = layout.body;
    canvas_fill_rect(canvas, body, theme->colors.accent);
    Rect inside = {body.x + 2, body.y + 2, body.width - 4, body.height - 4};
    canvas_fill_rect(canvas, inside, theme->colors.header_background);
    Rect label = {body.x + 8, body.y + 4, body.width - 16, layout.reels[0].y - body.y - 10};
    canvas_fill_rect(canvas, label, theme->colors.backdrop_top);
    canvas_fill_rect(canvas, (Rect){label.x, label.y + label.height - 3, label.width, 3},
                     theme->colors.accent_alt);
    canvas_fill_rect(canvas, layout.window, theme->colors.divider);
    Rect window = layout.window;
    canvas_fill_rect(canvas,
                     (Rect){window.x + 2, window.y + 2, window.width - 4, window.height - 4},
                     theme->colors.background);
    int y = body.y + body.height - 18;
    canvas_fill_rect(canvas, (Rect){body.x + body.width / 4, y, body.width / 2, 16},
                     theme->colors.divider);
    for (int corner = 0; corner < 4; corner++) {
        int x = body.x + ((corner & 1) != 0 ? body.width - 8 : 4);
        int sy = body.y + ((corner & 2) != 0 ? body.height - 8 : 4);
        disc(canvas, (Rect){x, sy, 4, 4}, theme->colors.text_tertiary);
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
    label_line(canvas, theme, (Label){layout.label[0], screen->title}, theme->fonts.title);
    label_line(canvas, theme, (Label){layout.label[1], screen->artist}, theme->fonts.body);
    label_line(canvas, theme, (Label){layout.label[2], screen->album}, theme->fonts.small);
    int y = layout.body.y + layout.body.height - 20;
    TextStyle footer = {.font = theme->fonts.small, .color = theme->colors.header_text};
    label_draw(canvas, footer,
               (Label){{layout.body.x + 12, y, 40, 18}, i18n_text(STR_CASSETTE_C60)});
    if (screen->tape_side == 1 || screen->tape_side == 2) {
        StringId side = screen->tape_side == 1 ? STR_CASSETTE_SIDE_A : STR_CASSETTE_SIDE_B;
        label_draw(canvas, footer,
                   (Label){{layout.body.x + layout.body.width - 30, y, 18, 18}, i18n_text(side)});
    }
    if (screen->scrubbing) {
        Rect head = {layout.body.x + layout.body.width / 4, y, layout.body.width / 2, 18};
        label_line(canvas, theme, (Label){head, i18n_text(STR_SEEKING)}, theme->fonts.small);
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
        StripRenderer renderer = {.strip_pixels = reel_pixels,
                                  .area = reel,
                                  .strip_height = REEL_PIXELS / reel.width,
                                  .background = screen->config.theme->colors.background};
        strip_renderer_render(&renderer, (FrameDrawer){draw_motion, screen},
                              (FrameSink){send_reels, NULL});
    }
    hal_display_present();
    return changed;
}
