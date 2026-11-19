#include <math.h>
#include <stdio.h>
#include <string.h>

#include "default_theme.h"
#include "fake_hal.h"
#include "hal/display.h"
#include "screens/meter_now_playing.h"
#include "screens/playing_work.h"
#include "sim_glyphs.h"

enum {
    CAPTURE_FRAMES = 4096,
    CAPTURE_TICKS = 26,
    MOTION_TICKS = 24,
    CAPTURE_SIDE = 480
};

typedef struct Capture {
    int style;
    int orientation;
    int size;
    int appearance;
    int level;
    bool motion;
    char name[96];
} Capture;

static const char *const LEVELS[] = {"quiet", "loud", "peak"};
static const double GAINS[] = {0.05, 0.42, 1.25};
static const char *const STYLES[] = {"", "", "vu", "spectrum"};
static const char *const SIZES[] = {"small", "medium", "large"};
static const char *const ORIENTATIONS[] = {"landscape", "portrait"};
static const char *const THEMES[] = {"graphite", "neon-noir", "sunset", "sea-glass", "classic"};

static Theme theme;
static Settings settings;
static NowPlayingScreen screen;
static Color pixels[CAPTURE_SIDE * CAPTURE_SIDE];
static uint32_t samples[CAPTURE_FRAMES * 2];
static const char *directory;
static int failures;

static double noise(unsigned *state)
{
    *state = *state * 1103515245u + 12345u;
    return (double)((*state >> 8) & 0xFFFF) / 32768.0 - 1.0;
}

static uint32_t sample_word(double value)
{
    double scaled = value * 32767.0;
    scaled = scaled > 32767 ? 32767 : scaled < -32768 ? -32768 : scaled;
    return (uint32_t)(int32_t)scaled << 16;
}

static void synthesize(int tick, int level)
{
    unsigned state = 1234u + (unsigned)tick * 77u;
    double beat = tick % 5 == 0 ? 1.0 : tick % 5 == 1 ? 0.55 : 0.2;
    double swell = 0.5 + 0.5 * sin(tick * 0.45);
    for (int frame = 0; frame < CAPTURE_FRAMES; frame++) {
        double t = (tick * 4410 + frame) / 44100.0;
        double low = beat * 0.6 * sin(2 * M_PI * 55 * t) + 0.25 * sin(2 * M_PI * 110 * t);
        double mids = swell * (0.18 * sin(2 * M_PI * 440 * t) + 0.12 * sin(2 * M_PI * 1320 * t));
        double air = 0.05 * sin(2 * M_PI * (2500 + 900 * swell) * t) + 0.06 * noise(&state);
        samples[frame * 2] = sample_word((low + mids + air) * GAINS[level]);
        samples[frame * 2 + 1] = sample_word((low + mids * 0.6 + air * 1.4) * GAINS[level] * 0.85);
    }
    fake_hal_set_visual_buffer(
        (AudioVisualBuffer){samples, CAPTURE_FRAMES, 0, CAPTURE_FRAMES, 44100});
}

static Canvas start(const Capture *capture)
{
    fake_hal_reset();
    sim_glyphs_attach();
    hal_display_set_rotation((HalDisplayRotation)capture->orientation);
    theme = BOARD_THEME;
    default_theme_set_appearance(&theme, (AppearanceTheme)capture->appearance);
    default_theme_set_text_size(&theme, (TextSize)capture->size);
    default_theme_set_orientation(&theme, (Orientation)capture->orientation);
    settings = settings_defaults();
    settings_set_now_playing_style(&settings, (NowPlayingStyle)capture->style);
    now_playing_screen_init(&screen,
                            (NowPlayingConfig){.theme = &theme,
                                               .settings = &settings,
                                               .cassette_draw = meter_now_playing_draw,
                                               .cassette_animate = meter_now_playing_animate});
    playing_module_work.owner = PLAYING_WORK_NONE;
    screen.status = (PlayerStatus){.has_track = true,
                                   .state = PLAYER_PLAYING,
                                   .duration_ms = 245000,
                                   .position_ms = 83000,
                                   .format = {HAL_AUDIO_CODEC_FLAC, 44100, 16, 2, 0}};
    strcpy(screen.title, "Palm Mall Afterglow");
    strcpy(screen.artist, "Cedar & Sky");
    strcpy(screen.album, "Plaza Nights");
    return canvas_make(pixels,
                       (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
}

static void full_frame(Canvas *canvas)
{
    canvas_clear(canvas, theme.colors.background);
    screen.screen.draw(&screen, canvas);
}

static bool inside(Rect area, int x, int y)
{
    return x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
}

static int settle_panel(Canvas *canvas)
{
    full_frame(canvas);
    Rect stage = meter_now_playing_layout(&theme).stage;
    int count = 0;
    for (int y = 0; y < canvas->bounds.height; y++) {
        for (int x = 0; x < canvas->bounds.width; x++) {
            Color drawn = pixels[y * canvas->bounds.width + x];
            if (inside(stage, x, y)) {
                count += fake_hal_pixel(x, y) != drawn ? 1 : 0;
            } else {
                hal_display_write(x, y, 1, 1, &drawn);
            }
        }
    }
    return count;
}

static void save_panel(const char *name, int width, int height)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", directory, name);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        failures++;
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t color = fake_hal_pixel(x, y);
            unsigned char rgb[3] = {(unsigned char)((color >> 11 & 31) * 255 / 31),
                                    (unsigned char)((color >> 5 & 63) * 255 / 63),
                                    (unsigned char)((color & 31) * 255 / 31)};
            fwrite(rgb, 1, sizeof rgb, file);
        }
    }
    fclose(file);
}

static int tick_once(Canvas *canvas, const Capture *capture, int tick)
{
    FrameDiff diff = {0};
    synthesize(tick, capture->level);
    screen.now_ms += AUDIO_VISUAL_FRAME_MS;
    screen.status.position_ms += AUDIO_VISUAL_FRAME_MS;
    meter_now_playing_animate(&screen, &diff, true);
    if (!capture->motion) {
        return 0;
    }
    int bad = settle_panel(canvas);
    char name[128];
    snprintf(name, sizeof name, "%s-%03d", capture->name, tick);
    save_panel(name, canvas->bounds.width, canvas->bounds.height);
    return bad;
}

static void run(const Capture *capture)
{
    Canvas canvas = start(capture);
    screen.now_ms = 1000;
    full_frame(&canvas);
    hal_display_write(0, 0, canvas.bounds.width, canvas.bounds.height, pixels);
    int ticks = capture->motion ? MOTION_TICKS : CAPTURE_TICKS;
    int bad = 0;
    for (int tick = 0; tick < ticks; tick++) {
        bad += tick_once(&canvas, capture, tick);
    }
    bad += settle_panel(&canvas);
    if (!capture->motion) {
        save_panel(capture->name, canvas.bounds.width, canvas.bounds.height);
    }
    if (bad != 0) {
        fprintf(stderr, "%s: %d pixels differ from a full redraw\n", capture->name, bad);
        failures++;
    }
}

static void sizes_and_levels(int style)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
            for (int level = 0; level < 3; level++) {
                Capture capture = {style, orientation, size, APPEARANCE_NEON_NOIR,
                                   level, false,       ""};
                snprintf(capture.name, sizeof capture.name, "%s-%s-%s-%s", STYLES[style],
                         ORIENTATIONS[orientation], SIZES[size], LEVELS[level]);
                run(&capture);
            }
        }
    }
}

static void themes_and_motion(int style)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        for (int appearance = 0; appearance < APPEARANCE_THEME_COUNT; appearance++) {
            Capture capture = {style, orientation, TEXT_SIZE_MEDIUM, appearance, 1, false, ""};
            snprintf(capture.name, sizeof capture.name, "theme-%s-%s-%s", STYLES[style],
                     ORIENTATIONS[orientation], THEMES[appearance]);
            run(&capture);
        }
        Capture motion = {style, orientation, TEXT_SIZE_MEDIUM, APPEARANCE_NEON_NOIR, 1, true, ""};
        snprintf(motion.name, sizeof motion.name, "motion-%s-%s", STYLES[style],
                 ORIENTATIONS[orientation]);
        run(&motion);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: meter_style_captures DIRECTORY\n");
        return 2;
    }
    directory = argv[1];
    for (int style = NOW_PLAYING_VU; style <= NOW_PLAYING_SPECTRUM; style++) {
        sizes_and_levels(style);
        themes_and_motion(style);
    }
    if (sim_glyphs_face_count() == 0) {
        fprintf(stderr, "no glyph faces: captures use the built-in fonts only\n");
        failures++;
    }
    return failures == 0 ? 0 : 1;
}
