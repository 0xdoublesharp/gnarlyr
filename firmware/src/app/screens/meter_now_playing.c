#include "meter_now_playing.h"

#include "core/hash.h"
#include "core/seam.h"
#include "gfx/strip_renderer.h"
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/display.h"
#include "neon_art.h"
#include "playing_work.h"
#include "sunset_art.h"
#include "ui/label.h"
#include "ui/toast.h"

enum {
    METER_PEAK_LEVEL = 30000,
    METER_PEAK_HOLD_MS = 500,
    METER_CAP_HOLD_MS = 400,
    METER_LONGEST_STEP_MS = 240
};

typedef struct MeterTick {
    uint8_t shown[2];
    bool lit[2];
    uint8_t bars[AUDIO_VISUAL_BANDS];
    uint8_t caps[AUDIO_VISUAL_BANDS];
    uint8_t pulse;
} MeterTick;

MeterNowPlayingLayout meter_now_playing_layout(const Theme *theme)
{
    bool portrait = theme_is_portrait(theme);
    int padding = theme->metrics.padding;
    int width = theme->metrics.screen_width - padding * 2;
    int title = font_line_height(theme->fonts.title);
    int artist = font_line_height(theme->fonts.body);
    if (portrait) {
        return (MeterNowPlayingLayout){
            .stage = {padding, 32, width, 246},
            .text = {{padding, 292, width, title}, {padding, 298 + title, width, artist}}};
    }
    return (MeterNowPlayingLayout){.stage = {padding, 38, 278, 150},
                                   .text = {{padding + 294, 48, width - 294, title},
                                            {padding + 294, 56 + title, width - 294, artist}}};
}

static MeterState *meter(void)
{
    return &playing_module_work.as.styles.meter;
}

void meter_now_playing_reset(void)
{
    *meter() = (MeterState){.frame = UINT32_MAX};
}

static SunsetArt art_of(const NowPlayingScreen *screen)
{
    const Theme *theme = screen->config.theme;
    bool vu = settings_now_playing_style(screen->config.settings) == NOW_PLAYING_VU;
    SunsetArt art = sunset_art_make(theme, meter_now_playing_layout(theme).stage, meter(), vu);
    const Toast *toast = screen->config.toast;
    art.now_ms = screen->now_ms;
    art.hole = toast != NULL && toast->visible ? toast_area(toast, theme) : (Rect){0};
    return art;
}

void meter_now_playing_draw(const NowPlayingScreen *screen, Canvas *canvas)
{
    const Theme *theme = screen->config.theme;
    MeterNowPlayingLayout layout = meter_now_playing_layout(theme);
    playing_work_styles();
    SunsetArt art = art_of(screen);
    sunset_art_draw(&art, canvas);
    label_draw(canvas, (TextStyle){theme->fonts.title, theme->colors.text, false},
               (Label){layout.text[0], screen->title});
    label_draw(canvas, (TextStyle){theme->fonts.body, theme->colors.text_secondary, false},
               (Label){layout.text[1], screen->artist});
}

static void draw_motion(void *context, Canvas *canvas)
{
    sunset_art_draw(context, canvas);
}

static void send_motion(void *context, Rect area, const Color *pixels)
{
    (void)context;
    hal_audio_service();
    hal_bluetooth_service();
    hal_display_write(area.x, area.y, area.width, area.height, pixels);
}

static void render_strips(SunsetArt *art, Rect visible)
{
    if (rect_is_empty(visible)) {
        return;
    }
    StripRenderer renderer = {.strip_pixels = playing_work_styles()->neon.pixels,
                              .area = visible,
                              .strip_height = PLAYING_NEON_PIXELS / visible.width,
                              .drawer_paints_everything = true};
    strip_renderer_render(&renderer, (FrameDrawer){draw_motion, art},
                          (FrameSink){send_motion, NULL});
}

static void render(SunsetArt *art, Rect area)
{
    Rect visible = rect_intersection(area, art->scene.stage);
    Rect hole = rect_intersection(visible, art->hole);
    if (rect_is_empty(hole)) {
        render_strips(art, visible);
        return;
    }
    int right = hole.x + hole.width;
    int bottom = hole.y + hole.height;
    render_strips(art, (Rect){visible.x, visible.y, visible.width, hole.y - visible.y});
    render_strips(art,
                  (Rect){visible.x, bottom, visible.width, visible.y + visible.height - bottom});
    render_strips(art, (Rect){visible.x, hole.y, hole.x - visible.x, hole.height});
    render_strips(art, (Rect){right, hole.y, visible.x + visible.width - right, hole.height});
}

static Rect rows_between(int x, int width, int first, int second)
{
    int top = first < second ? first : second;
    int bottom = first > second ? first : second;
    return (Rect){x, top, width, bottom - top};
}

static void render_sun(SunsetArt *art, const MeterTick *before)
{
    const SunsetScene *scene = &art->scene;
    for (int side = 0; side < 2; side++) {
        Rect half = sunset_scene_half(scene, side);
        int was = sunset_scene_lit_top(scene, before->shown[side]);
        int now = sunset_scene_lit_top(scene, art->meter->shown[side]);
        if (was != now) {
            render(art, rows_between(half.x, half.width, was, now));
        }
        if (before->lit[side] != sunset_art_peak_lit(art->meter, side, art->now_ms)) {
            Rect peak = scene->peaks[side];
            render(art, (Rect){half.x, half.y, half.width, SUNSET_FLASH_ROWS});
            render(art, (Rect){peak.x - 1, peak.y, peak.width + 3, peak.height + 2});
        }
    }
}

static void render_caps(SunsetArt *art, Rect column, uint8_t was, uint8_t now)
{
    int first = sunset_scene_bar_top(&art->scene, was) - 3;
    int second = sunset_scene_bar_top(&art->scene, now) - 3;
    int distance = first > second ? first - second : second - first;
    if (distance < 4) {
        render(art, rows_between(column.x, column.width, first < second ? first : second,
                                 (first > second ? first : second) + 2));
        return;
    }
    render(art, (Rect){column.x, first, column.width, 2});
    render(art, (Rect){column.x, second, column.width, 2});
}

static void render_band(SunsetArt *art, const MeterTick *before, int band)
{
    const SunsetScene *scene = &art->scene;
    Rect column = sunset_scene_bar(scene, band);
    uint8_t level = art->meter->bars[band];
    int was = sunset_scene_bar_top(scene, before->bars[band]);
    int now = sunset_scene_bar_top(scene, level);
    if (was != now) {
        render(art, rows_between(column.x - 1, column.width + 2, (was < now ? was : now) - 1,
                                 (was > now ? was : now) + 1));
        int deep = sunset_scene_reflection(scene, before->bars[band]);
        int depth = sunset_scene_reflection(scene, level);
        if (deep != depth) {
            render(art, rows_between(column.x, column.width, scene->horizon + deep,
                                     scene->horizon + depth));
        }
    }
    if (before->caps[band] != art->meter->caps[band]) {
        render_caps(art, column, before->caps[band], art->meter->caps[band]);
    }
}

static void render_floor(SunsetArt *art)
{
    Rect floor = sunset_scene_floor(&art->scene);
    for (int line = 0; line < SUNSET_FLOOR_LINES; line++) {
        render(art, (Rect){floor.x, sunset_scene_floor_line(&art->scene, line), floor.width, 1});
    }
}

static void render_changes(SunsetArt *art, const MeterTick *before)
{
    if (before->pulse != art->meter->pulse) {
        render_floor(art);
    }
    if (art->sun_meter) {
        render_sun(art, before);
        return;
    }
    for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
        render_band(art, before, band);
    }
}

static MeterTick tick_of(const MeterState *state)
{
    MeterTick tick = {.pulse = state->pulse};
    for (int side = 0; side < 2; side++) {
        tick.shown[side] = state->shown[side];
        tick.lit[side] = sunset_art_peak_lit(state, side, state->updated_ms);
    }
    for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
        tick.bars[band] = state->bars[band];
        tick.caps[band] = state->caps[band];
    }
    return tick;
}

static uint8_t falling(uint8_t value, unsigned fall)
{
    return value > fall ? (uint8_t)(value - fall) : 0;
}

static void update_bars(MeterState *state, const AudioVisualLevels *levels, uint32_t elapsed,
                        uint32_t now)
{
    for (int band = 0; band < AUDIO_VISUAL_BANDS; band++) {
        uint8_t next = levels->band[band];
        uint8_t lower = falling(state->bars[band], elapsed / 4);
        state->bars[band] = next > lower ? next : lower;
        if (next >= state->caps[band] && next > 0) {
            state->caps[band] = next;
            state->cap_until[band] = now + METER_CAP_HOLD_MS;
        } else if ((int32_t)(now - state->cap_until[band]) >= 0) {
            state->caps[band] = falling(state->caps[band], elapsed / 12);
        }
    }
}

static void update_needles(MeterState *state, const AudioVisualLevels *levels, uint32_t elapsed,
                           uint32_t now)
{
    for (int channel = 0; channel < 2; channel++) {
        audio_visual_needle(&state->needles[channel], levels->mean[channel], elapsed);
        state->shown[channel] = audio_visual_db((uint32_t)state->needles[channel].position / 256);
        if (levels->peak[channel] >= METER_PEAK_LEVEL) {
            state->peak_until[channel] = now + METER_PEAK_HOLD_MS;
        }
    }
}

static uint8_t energy_of(const AudioVisualLevels *levels, bool vu)
{
    if (vu) {
        return audio_visual_db(levels->bass);
    }
    uint8_t energy = levels->band[0] > levels->band[1] ? levels->band[0] : levels->band[1];
    return energy > levels->band[2] ? energy : levels->band[2];
}

static void update(NowPlayingScreen *screen, const AudioVisualLevels *levels, uint32_t elapsed)
{
    MeterState *state = meter();
    bool vu = settings_now_playing_style(screen->config.settings) == NOW_PLAYING_VU;
    if (vu) {
        update_needles(state, levels, elapsed, screen->now_ms);
    } else {
        update_bars(state, levels, elapsed, screen->now_ms);
    }
    uint8_t pulse = sunset_scene_pulse(state->pulse, energy_of(levels, vu));
    state->pulse = pulse < state->pulse && state->frame % 2 != 0 ? state->pulse : pulse;
}

static bool rest(MeterState *state, FrameDiff *diff)
{
    state->active = false;
    bool changed = !rect_is_empty(diff->masked);
    if (changed) {
        frame_diff_mask(diff, (Rect){0});
    }
    return changed;
}

static uint32_t look_of(const Theme *theme)
{
    uint32_t look = hash_update(HASH_START, &theme->colors, sizeof theme->colors);
    look = hash_update(look, &theme->metrics.screen_width, sizeof theme->metrics.screen_width);
    return hash_update(look, &theme->fonts.title, sizeof theme->fonts.title);
}

static void follow(MeterState *state, const NowPlayingScreen *screen, NowPlayingStyle style)
{
    uint32_t look = look_of(screen->config.theme);
    if (state->style != style || state->track != screen->status.track ||
        state->text_changed_ms != screen->text_changed_ms || state->look != look) {
        meter_now_playing_reset();
        state->style = style;
        state->track = screen->status.track;
        state->text_changed_ms = screen->text_changed_ms;
        state->look = look;
        state->repaint = true;
    }
}

static bool mask_stage(const NowPlayingScreen *screen, FrameDiff *diff)
{
    Rect mask = meter_now_playing_layout(screen->config.theme).stage;
    bool changed = diff->masked.x != mask.x || diff->masked.y != mask.y ||
                   diff->masked.width != mask.width || diff->masked.height != mask.height;
    if (changed) {
        frame_diff_mask(diff, mask);
    }
    return changed;
}

static AudioVisualLevels measure(const NowPlayingScreen *screen, NowPlayingStyle style)
{
    AudioVisualLevels levels = {0};
    if (screen->status.state == PLAYER_PLAYING) {
        levels = audio_visual_measure(neon_art_audio_work(), hal_audio_visual_buffer(),
                                      style == NOW_PLAYING_SPECTRUM);
    }
    return levels;
}

static void step(NowPlayingScreen *screen, NowPlayingStyle style)
{
    MeterState *state = meter();
    uint32_t elapsed = state->active ? screen->now_ms - state->updated_ms : AUDIO_VISUAL_FRAME_MS;
    elapsed = elapsed > METER_LONGEST_STEP_MS ? METER_LONGEST_STEP_MS : elapsed;
    state->active = true;
    state->frame = screen->now_ms / AUDIO_VISUAL_FRAME_MS;
    AudioVisualLevels levels = measure(screen, style);
    MeterTick before = tick_of(state);
    update(screen, &levels, elapsed);
    SunsetArt art = art_of(screen);
    if (state->repaint) {
        state->repaint = false;
        render(&art, art.scene.stage);
    } else {
        render_changes(&art, &before);
    }
    state->updated_ms = screen->now_ms;
    hal_display_present();
}

CORE_SEAM bool meter_now_playing_animate(NowPlayingScreen *screen, FrameDiff *diff, bool visible)
{
    MeterState *state = &playing_work_styles()->meter;
    bool active = visible && screen->status.has_track &&
                  !now_playing_feedback_visible(screen->feedback, screen->feedback_started_ms,
                                                screen->now_ms);
    if (!active) {
        return rest(state, diff);
    }
    NowPlayingStyle style = settings_now_playing_style(screen->config.settings);
    bool due = !state->active || state->frame != screen->now_ms / AUDIO_VISUAL_FRAME_MS;
    if (due) {
        follow(state, screen, style);
    }
    bool changed = mask_stage(screen, diff);
    if (due) {
        step(screen, style);
    }
    return changed;
}
