#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "screen_snapshot.h"
#include "text_spy.h"
#include "ui/message_screen.h"
#include "ui/wire_motion.h"

enum {
    VARIANTS = 2 * TEXT_SIZE_COUNT,
    SEQUENCE_FRAMES = 16,
    START_MS = 1000
};

typedef struct WorkingCase {
    const char *name;
    StringId title;
    StringId message;
    WireShape shape;
    uint32_t done;
    uint32_t total;
} WorkingCase;

static const WorkingCase CASES[] = {
    {"scanning", STR_SCANNING, STR_SCAN_STARTING, WIRE_SHAPE_DISC, 0, 0},
    {"covers", STR_FINDING_COVERS, STR_SCAN_STARTING, WIRE_SHAPE_DISC, 3, 8},
    {"mixtape", STR_SAVING_MIXTAPE, STR_BACK_CANCELS, WIRE_SHAPE_CASSETTE, 0, 0},
    {"searching", STR_SEARCH_RESULTS, STR_SEARCHING_MUSIC, WIRE_SHAPE_DISC, 40, 100},
    {"tags", STR_SEARCH_RESULTS, STR_LOADING_TAGS, WIRE_SHAPE_ICOSAHEDRON, 0, 0},
    {"saving-tags", STR_SEARCH_RESULTS, STR_SAVING_TAGS, WIRE_SHAPE_CASSETTE, 0, 0},
};

static const AppearanceTheme APPEARANCES[] = {APPEARANCE_GRAPHITE, APPEARANCE_SUNSET,
                                              APPEARANCE_CLASSIC};

static Color pixels[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
static Color before[HAL_DISPLAY_WIDTH * HAL_DISPLAY_HEIGHT];
static Theme theme;
static WireMotion motion;
static MessageScreen message;

static bool saw_whole(const char *text)
{
    for (int index = 0; index < text_spy_count(); index++) {
        if (strcmp(text_spy_entry(index)->rest, text) == 0) {
            return true;
        }
    }
    return false;
}

static void use_theme(int variant, AppearanceTheme appearance)
{
    theme = BOARD_THEME;
    default_theme_set_appearance(&theme, appearance);
    default_theme_set_text_size(&theme, (TextSize)(variant % TEXT_SIZE_COUNT));
    default_theme_set_orientation(&theme, (Orientation)(variant / TEXT_SIZE_COUNT));
}

static Canvas draw(const WorkingCase *working)
{
    Canvas canvas =
        canvas_make(pixels, (Rect){0, 0, theme.metrics.screen_width, theme.metrics.screen_height});
    MessageConfig config = {.title = i18n_text(working->title),
                            .message = i18n_text(working->message),
                            .theme = &theme,
                            .motion = &motion};
    Screen *screen = message_screen_init(&message, config);
    canvas_fill_rect(&canvas, canvas.bounds, theme.colors.background);
    text_spy_reset();
    screen->draw(screen->context, &canvas);
    return canvas;
}

static void start(const WorkingCase *working)
{
    motion = (WireMotion){0};
    wire_motion_run(&motion, &message, working->shape, START_MS);
    wire_motion_progress(&motion, working->done, working->total);
}

static int changed_outside(const Canvas *canvas, Rect area)
{
    int count = 0;
    for (int y = 0; y < canvas->bounds.height; y++) {
        for (int x = 0; x < canvas->bounds.width; x++) {
            bool inside =
                x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
            int index = y * canvas->bounds.width + x;
            count += !inside && pixels[index] != before[index];
        }
    }
    return count;
}

static int changed_inside(const Canvas *canvas, Rect area)
{
    int count = 0;
    for (int y = area.y; y < area.y + area.height; y++) {
        for (int x = area.x; x < area.x + area.width; x++) {
            int index = y * canvas->bounds.width + x;
            count += pixels[index] != before[index];
        }
    }
    return count;
}

static void a_working_message_keeps_its_text_whole_and_draws_only_inside_its_square(void)
{
    for (size_t index = 0; index < sizeof CASES / sizeof *CASES; index++) {
        for (int variant = 0; variant < VARIANTS; variant++) {
            use_theme(variant, APPEARANCE_GRAPHITE);
            start(&CASES[index]);
            Canvas canvas = draw(&CASES[index]);
            CHECK(saw_whole(i18n_text(CASES[index].message)));
            memcpy(before, pixels, sizeof pixels);
            wire_motion_tick(&motion, START_MS + WIRE_MOTION_DELAY_MS);
            draw(&CASES[index]);
            CHECK(motion.area.width >= WIRE_MOTION_MIN_SIDE);
            CHECK(motion.area.width <= WIRE_MOTION_MAX_SIDE);
            CHECK(changed_inside(&canvas, motion.area) > 0);
            CHECK_EQUAL(0, changed_outside(&canvas, motion.area));
        }
    }
}

static void write_named(const Canvas *canvas, const char *stem, const char *part, int variant)
{
    char name[64];
    StringBuilder builder = string_builder_make(name, sizeof name);
    string_builder_append(&builder, stem);
    string_builder_append(&builder, part);
    screen_snapshot_write(canvas, name, variant);
}

static void working_messages_render_at_every_size_orientation_and_theme(void)
{
    for (size_t index = 0; index < sizeof CASES / sizeof *CASES; index++) {
        for (size_t look = 0; look < sizeof APPEARANCES / sizeof *APPEARANCES; look++) {
            for (int variant = 0; variant < VARIANTS; variant++) {
                use_theme(variant, APPEARANCES[look]);
                start(&CASES[index]);
                wire_motion_tick(&motion, START_MS + WIRE_MOTION_DELAY_MS + 400);
                Canvas canvas = draw(&CASES[index]);
                CHECK(saw_whole(i18n_text(CASES[index].message)));
                write_named(&canvas, "working-", CASES[index].name, (int)look * VARIANTS + variant);
            }
        }
    }
}

static void a_working_sequence_moves_on_every_tick(void)
{
    for (size_t index = 0; index < sizeof CASES / sizeof *CASES; index++) {
        use_theme(0, APPEARANCE_GRAPHITE);
        start(&CASES[index]);
        for (int frame = 0; frame < SEQUENCE_FRAMES; frame++) {
            uint32_t now = START_MS + WIRE_MOTION_DELAY_MS + (uint32_t)frame * WIRE_MOTION_FRAME_MS;
            CHECK(wire_motion_due(&motion, now));
            wire_motion_tick(&motion, now);
            CHECK(!wire_motion_due(&motion, now + 1));
            Canvas canvas = draw(&CASES[index]);
            if (frame > 0) {
                CHECK(changed_inside(&canvas, motion.area) > 0);
                CHECK_EQUAL(0, changed_outside(&canvas, motion.area));
            }
            memcpy(before, pixels, sizeof pixels);
            write_named(&canvas, "sequence-", CASES[index].name, frame);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_working_message_keeps_its_text_whole_and_draws_only_inside_its_square),
        TEST_CASE(working_messages_render_at_every_size_orientation_and_theme),
        TEST_CASE(a_working_sequence_moves_on_every_tick),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
