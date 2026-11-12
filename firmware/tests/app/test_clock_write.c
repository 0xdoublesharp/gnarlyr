#include "check.h"
#include "extension_entry.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"

static bool loaded;
static bool accepted;
static int writes;
static int calls;
static HalWallTime written;

static void motion_run(WireMotion *motion, const void *owner, WireShape shape, uint32_t now_ms)
{
    (void)motion;
    (void)owner;
    (void)shape;
    (void)now_ms;
    calls++;
}

static void motion_progress(WireMotion *motion, uint32_t done, uint32_t total)
{
    (void)motion;
    (void)done;
    (void)total;
    calls++;
}

static void motion_end(WireMotion *motion, const void *owner)
{
    (void)motion;
    (void)owner;
    calls++;
}

static bool motion_shown(const WireMotion *motion, const void *owner)
{
    (void)motion;
    (void)owner;
    calls++;
    return accepted;
}

static bool motion_due(const WireMotion *motion, uint32_t now_ms)
{
    (void)motion;
    (void)now_ms;
    calls++;
    return accepted;
}

static void motion_tick(WireMotion *motion, uint32_t now_ms)
{
    (void)motion;
    (void)now_ms;
    calls++;
}

static Rect motion_fit(Rect space)
{
    calls++;
    return (Rect){space.x + 1, space.y + 2, space.width - 3, space.height - 4};
}

static void motion_draw(WireMotion *motion, Canvas *canvas, const Theme *theme, Rect space)
{
    (void)motion;
    (void)canvas;
    (void)theme;
    (void)space;
    calls++;
}

static bool file_remove(const char *path)
{
    (void)path;
    calls++;
    return accepted;
}

static void headphones_load(HeadphoneStore *store)
{
    (void)store;
    calls++;
}

static bool set_clock(HalWallTime time)
{
    writes++;
    written = time;
    return accepted;
}

static bool restore_clock(int16_t offset)
{
    writes++;
    written.offset_minutes = offset;
    return accepted;
}

static const ExtensionModuleEntry code = {
    .motion_run = motion_run,
    .motion_progress = motion_progress,
    .motion_end = motion_end,
    .motion_shown = motion_shown,
    .motion_due = motion_due,
    .motion_tick = motion_tick,
    .motion_fit = motion_fit,
    .motion_draw = motion_draw,
    .file_remove = file_remove,
    .headphones_load = headphones_load,
    .clock_set = set_clock,
    .clock_restore = restore_clock,
};

void hal_audio_set_headphones(HeadphoneAudio profile)
{
    (void)profile;
}

const void *hal_system_extension_code(void)
{
    return loaded ? &code : NULL;
}

static void missing_extension_refuses_without_writing(void)
{
    loaded = false;
    writes = 0;
    CHECK(!hal_clock_set_wall((HalWallTime){.utc_s = 1794390060u}));
    CHECK(!hal_clock_restore_wall(-480));
    CHECK_EQUAL(0, writes);
}

static void the_write_result_and_time_pass_through_the_extension(void)
{
    loaded = true;
    accepted = false;
    writes = 0;
    HalWallTime wanted = {.utc_s = 1794390060u, .offset_minutes = -480, .source = HAL_WALL_PLAYER};
    CHECK(!hal_clock_set_wall(wanted));
    CHECK_EQUAL(1, writes);
    accepted = true;
    CHECK(hal_clock_set_wall(wanted));
    CHECK_EQUAL(2, writes);
    CHECK_EQUAL(wanted.utc_s, written.utc_s);
    CHECK_EQUAL(wanted.offset_minutes, written.offset_minutes);
    CHECK(hal_clock_restore_wall(345));
    CHECK_EQUAL(345, written.offset_minutes);
}

static void extension_shims_are_safe_when_the_extension_is_missing(void)
{
    loaded = false;
    calls = 0;
    WireMotion motion = {0};
    Rect space = {1, 2, 20, 30};
    int owner = 0;
    HeadphoneStore headphones = {0};

    wire_motion_run(&motion, &owner, WIRE_SHAPE_DISC, 10);
    wire_motion_progress(&motion, 1, 2);
    wire_motion_end(&motion, &owner);
    CHECK(!wire_motion_shown(&motion, &owner));
    CHECK(!wire_motion_due(&motion, 20));
    wire_motion_tick(&motion, 30);
    Rect fitted = wire_motion_fit(space);
    CHECK_EQUAL(space.x, fitted.x);
    CHECK_EQUAL(space.y, fitted.y);
    CHECK_EQUAL(0, fitted.width);
    CHECK_EQUAL(0, fitted.height);
    wire_motion_draw(&motion, NULL, NULL, space);
    CHECK(!hal_file_remove("/missing"));
    headphone_store_load(&headphones);
    CHECK_EQUAL(0, calls);
    CHECK(headphones.file.needs_reload);
}

static void extension_shims_forward_to_the_loaded_extension(void)
{
    loaded = true;
    accepted = true;
    calls = 0;
    WireMotion motion = {0};
    Rect space = {1, 2, 20, 30};
    int owner = 0;
    HeadphoneStore headphones = {0};

    wire_motion_run(&motion, &owner, WIRE_SHAPE_DISC, 10);
    wire_motion_progress(&motion, 1, 2);
    wire_motion_end(&motion, &owner);
    CHECK(wire_motion_shown(&motion, &owner));
    CHECK(wire_motion_due(&motion, 20));
    wire_motion_tick(&motion, 30);
    Rect fitted = wire_motion_fit(space);
    CHECK_EQUAL(2, fitted.x);
    CHECK_EQUAL(4, fitted.y);
    CHECK_EQUAL(17, fitted.width);
    CHECK_EQUAL(26, fitted.height);
    wire_motion_draw(&motion, NULL, NULL, space);
    CHECK(hal_file_remove("/present"));
    headphone_store_load(&headphones);
    CHECK_EQUAL(10, calls);
    accepted = false;
    CHECK(!wire_motion_shown(&motion, &owner));
    CHECK(!wire_motion_due(&motion, 50));
    CHECK(!hal_file_remove("/refused"));
    CHECK_EQUAL(13, calls);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(missing_extension_refuses_without_writing),
        TEST_CASE(the_write_result_and_time_pass_through_the_extension),
        TEST_CASE(extension_shims_are_safe_when_the_extension_is_missing),
        TEST_CASE(extension_shims_forward_to_the_loaded_extension),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
