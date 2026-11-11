#include "board_layout.h"
#include "check.h"
#include "default_theme.h"
#include "hal/display.h"
#include "ui/icons.h"

enum {
    CANVAS_SIZE = 84,
    ORIGIN = 12,
    SMALLEST_SIDE = 4,
    LARGEST_SIDE = 60,
    WHOLE_SCALES = 3,
    ODD_SIDE = 26,
    ODD_STROKE = 3,
    MENU_BARS = 3,
    BACKGROUND = 0x0821,
    MAIN = 0xFFFF,
    ACCENT = 0xF800
};

static const IconInk INK = {MAIN, ACCENT};

static const uint8_t MIRRORED[] = {
    ICON_PODCAST,  ICON_AUDIO,     ICON_COVER_FLOW,  ICON_ARTIST, ICON_FAVORITE,
    ICON_DISPLAY,  ICON_POWER,     ICON_CONTROLS,    ICON_START,  ICON_JACK,
    ICON_KEY_PLUS, ICON_KEY_MINUS, ICON_KEY_MENU,    ICON_DISC,   ICON_HUB,
    ICON_PAUSE,    ICON_SEEK,      ICON_ALBUM_SMALL, ICON_LOCK,
};

static const uint8_t FLIPPED[] = {
    ICON_GENRE,
    ICON_SHUFFLE,
    ICON_THEME,
    ICON_SLEEP,
    ICON_BLUETOOTH,
    ICON_KEY_PLUS,
    ICON_KEY_MINUS,
    ICON_KEY_MENU,
    ICON_DISC,
    ICON_HUB,
    ICON_PLAY,
    ICON_PAUSE,
    ICON_NEXT,
    ICON_PREVIOUS,
    ICON_SEEK,
    ICON_SPEAKER,
    ICON_ALBUM_SMALL,
    ICON_SHUFFLE_SMALL,
    ICON_BLUETOOTH_SMALL,
};

static const uint32_t SHAPES[ICON_COUNT] = {
    [ICON_NONE] = 0x00000000,         [ICON_MUSIC] = 0x8C736B0A,
    [ICON_AUDIOBOOK] = 0xE90008C1,    [ICON_PODCAST] = 0x24EEB073,
    [ICON_NOW_PLAYING] = 0x6D742C47,  [ICON_MIXTAPE] = 0xB8A6E883,
    [ICON_AUDIO] = 0x46C747C1,        [ICON_SETTINGS] = 0xB31486E1,
    [ICON_COVER_FLOW] = 0xD9C864D5,   [ICON_ARTIST] = 0x028C1E81,
    [ICON_ALBUM_ARTIST] = 0x32F0C6D5, [ICON_ALBUM] = 0x83CE6979,
    [ICON_GENRE] = 0x1FC98BFD,        [ICON_FOLDER] = 0x823120A9,
    [ICON_SONG] = 0x46395B4F,         [ICON_PLAYLIST] = 0x5C1D410F,
    [ICON_FAVORITE] = 0x3097EBB5,     [ICON_SHUFFLE] = 0x4AD0B802,
    [ICON_REPEAT] = 0xF6F17BD1,       [ICON_DISPLAY] = 0x3BFC49B1,
    [ICON_THEME] = 0xF562B02D,        [ICON_POWER] = 0xEF225AA3,
    [ICON_SLEEP] = 0x5288ECE9,        [ICON_TIMEOUT] = 0xE35E07CB,
    [ICON_CONTROLS] = 0xA7F1E9A9,     [ICON_START] = 0xB28DA15D,
    [ICON_RESCAN] = 0xD98FE13D,       [ICON_DIAGNOSTICS] = 0x2B171105,
    [ICON_GAIN] = 0xC875ED0D,         [ICON_JACK] = 0x9EACD88D,
    [ICON_USB] = 0x907285B1,          [ICON_BLUETOOTH] = 0x01F30915,
    [ICON_KEY_PLUS] = 0x861C7F19,     [ICON_KEY_MINUS] = 0x8F6E6859,
    [ICON_KEY_PLAY] = 0x296191DB,     [ICON_KEY_MENU] = 0x915B7939,
    [ICON_DISC] = 0x9C6F6D25,         [ICON_HUB] = 0x1ACF85C1,
    [ICON_SEARCH] = 0x2AC8F027,       [ICON_PLAY] = 0xAC5DDDCD,
    [ICON_PAUSE] = 0xA739DBA5,        [ICON_NEXT] = 0xE55CF759,
    [ICON_PREVIOUS] = 0x523E8999,     [ICON_SEEK] = 0xE7C1C5FD,
    [ICON_QUEUE] = 0x939F1D1B,        [ICON_SPEAKER] = 0xC62C77C9,
    [ICON_ALBUM_SMALL] = 0x739C8001,  [ICON_SHUFFLE_SMALL] = 0x89E8BE0D,
    [ICON_BOLT] = 0x85A70DD8,         [ICON_BLUETOOTH_SMALL] = 0xF4B5F2E3,
    [ICON_LOCK] = 0x0849F8C2,         [ICON_SONG_SMALL] = 0xCB86ADB1,
};

static Color pixels[CANVAS_SIZE * CANVAS_SIZE];

static Canvas blank_canvas(void)
{
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, CANVAS_SIZE, CANVAS_SIZE});
    canvas_clear(&canvas, BACKGROUND);
    return canvas;
}

static Rect area_of_height(IconId icon, int height)
{
    return (Rect){ORIGIN, ORIGIN, icon_width(icon, height), height};
}

static Rect natural_area(IconId icon)
{
    return area_of_height(icon, icon_rows(icon));
}

static bool inside(Rect area, int x, int y)
{
    return x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
}

static int count_ink(const Canvas *canvas, Rect area, bool within)
{
    int count = 0;
    for (int index = 0; index < CANVAS_SIZE * CANVAS_SIZE; index++) {
        bool drawn = canvas->pixels[index] != BACKGROUND;
        count += drawn && inside(area, index % CANVAS_SIZE, index / CANVAS_SIZE) == within ? 1 : 0;
    }
    return count;
}

static int count_color(const Canvas *canvas, Color color)
{
    int count = 0;
    for (int index = 0; index < CANVAS_SIZE * CANVAS_SIZE; index++) {
        count += canvas->pixels[index] == color ? 1 : 0;
    }
    return count;
}

static uint32_t shape_hash(const Canvas *canvas, Rect area)
{
    uint32_t hash = 2166136261u;
    for (int y = area.y; y < area.y + area.height; y++) {
        for (int x = area.x; x < area.x + area.width; x++) {
            Color color = canvas_pixel_at(canvas, x, y);
            hash = (hash ^ (color == MAIN ? 1u : color == ACCENT ? 2u : 0u)) * 16777619u;
        }
    }
    return hash;
}

static uint32_t drawn_hash(IconId icon, Rect area)
{
    Canvas canvas = blank_canvas();
    icon_draw(&canvas, icon, area, INK);
    return shape_hash(&canvas, canvas.bounds);
}

static bool line_has_ink(const Canvas *canvas, int line, bool column)
{
    for (int along = 0; along < CANVAS_SIZE; along++) {
        Color color =
            column ? canvas_pixel_at(canvas, line, along) : canvas_pixel_at(canvas, along, line);
        if (color != BACKGROUND) {
            return true;
        }
    }
    return false;
}

static int ink_edge(const Canvas *canvas, bool column, bool far)
{
    for (int step = 0; step < CANVAS_SIZE; step++) {
        int line = far ? CANVAS_SIZE - 1 - step : step;
        if (line_has_ink(canvas, line, column)) {
            return line;
        }
    }
    return 0;
}

static bool ink_mirrors(const Canvas *canvas, bool flipped)
{
    int left = ink_edge(canvas, true, false);
    int right = ink_edge(canvas, true, true);
    int top = ink_edge(canvas, false, false);
    int bottom = ink_edge(canvas, false, true);
    for (int y = top; y <= bottom; y++) {
        for (int x = left; x <= right; x++) {
            Color twin = flipped ? canvas_pixel_at(canvas, x, top + bottom - y)
                                 : canvas_pixel_at(canvas, left + right - x, y);
            if (canvas_pixel_at(canvas, x, y) != twin) {
                return false;
            }
        }
    }
    return true;
}

static bool listed(const uint8_t *list, size_t count, IconId icon)
{
    for (size_t index = 0; index < count; index++) {
        if (list[index] == icon) {
            return true;
        }
    }
    return false;
}

static void check_symmetry(const Canvas *canvas, IconId icon)
{
    if (listed(MIRRORED, sizeof MIRRORED, icon)) {
        CHECK(ink_mirrors(canvas, false));
    }
    if (listed(FLIPPED, sizeof FLIPPED, icon)) {
        CHECK(ink_mirrors(canvas, true));
    }
}

static void check_whole(IconId icon, Rect area)
{
    Canvas canvas = blank_canvas();
    icon_draw(&canvas, icon, area, INK);
    CHECK(count_ink(&canvas, area, true) > 0);
    CHECK_EQUAL(0, count_ink(&canvas, area, false));
    check_symmetry(&canvas, icon);
}

static void draw_in_strips(Canvas *canvas, IconId icon, Rect area, int strip_height)
{
    for (int top = 0; top < CANVAS_SIZE; top += strip_height) {
        Canvas strip = canvas_clipped(canvas, (Rect){0, top, CANVAS_SIZE, strip_height});
        icon_draw(&strip, icon, area, INK);
    }
}

static Theme theme_for(int size, Orientation orientation)
{
    Theme theme = BOARD_THEME;
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, orientation);
    return theme;
}

static int themed_height(const Theme *theme, IconId icon)
{
    return icon_rows(icon) == ICON_SMALL_GRID ? theme->metrics.status_icon_size : icon_rows(icon);
}

static void check_on_screen(const Theme *theme, IconId icon)
{
    int strip_height = HAL_DISPLAY_WIDTH * BOARD_STRIP_HEIGHT / theme->metrics.screen_width;
    int height = themed_height(theme, icon);
    Rect area = {ORIGIN, strip_height - height / 2, icon_width(icon, height), height};
    check_whole(icon, area);
    uint32_t whole = drawn_hash(icon, area);
    Canvas canvas = blank_canvas();
    draw_in_strips(&canvas, icon, area, strip_height);
    CHECK_EQUAL(whole, shape_hash(&canvas, canvas.bounds));
}

static void every_icon_is_whole_at_each_text_size_in_both_orientations(void)
{
    static const Orientation orientations[] = {ORIENTATION_LANDSCAPE, ORIENTATION_PORTRAIT};
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        for (int turn = 0; turn < 2; turn++) {
            Theme theme = theme_for(size, orientations[turn]);
            for (int icon = ICON_NONE + 1; icon < ICON_COUNT; icon++) {
                check_on_screen(&theme, (IconId)icon);
            }
        }
    }
}

static void a_large_icon_fits_a_list_row_at_every_text_size(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_for(size, ORIENTATION_LANDSCAPE);
        CHECK(ICON_LARGE_GRID <= theme.metrics.row_height);
    }
}

static void status_icons_are_drawn_at_a_whole_multiple_of_their_grid(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = theme_for(size, ORIENTATION_PORTRAIT);
        CHECK(theme.metrics.status_icon_size >= ICON_SMALL_GRID);
        CHECK_EQUAL(0, theme.metrics.status_icon_size % ICON_SMALL_GRID);
    }
}

static void every_icon_stays_inside_its_area_at_any_size(void)
{
    for (int side = SMALLEST_SIDE; side <= LARGEST_SIDE; side++) {
        for (int icon = ICON_NONE + 1; icon < ICON_COUNT; icon++) {
            Canvas canvas = blank_canvas();
            Rect area = {ORIGIN, ORIGIN, side, side};
            icon_draw(&canvas, (IconId)icon, area, INK);
            CHECK(count_ink(&canvas, area, true) > 0);
            CHECK_EQUAL(0, count_ink(&canvas, area, false));
        }
    }
}

static void symmetric_icons_stay_symmetric_at_whole_multiples_of_their_grid(void)
{
    for (int scale = 1; scale <= WHOLE_SCALES; scale++) {
        for (int icon = ICON_NONE + 1; icon < ICON_COUNT; icon++) {
            check_whole((IconId)icon,
                        area_of_height((IconId)icon, scale * icon_rows((IconId)icon)));
        }
    }
}

static void every_icon_keeps_its_drawn_shape(void)
{
    for (int icon = ICON_NONE + 1; icon < ICON_COUNT; icon++) {
        Canvas canvas = blank_canvas();
        Rect area = natural_area((IconId)icon);
        icon_draw(&canvas, (IconId)icon, area, INK);
        CHECK_EQUAL(SHAPES[icon], shape_hash(&canvas, area));
    }
}

static void a_narrow_icon_is_nine_columns_wide_for_fourteen_rows(void)
{
    CHECK_EQUAL(ICON_NARROW_COLUMNS, icon_width(ICON_LOCK, ICON_SMALL_GRID));
    CHECK_EQUAL(2 * ICON_NARROW_COLUMNS, icon_width(ICON_BOLT, 2 * ICON_SMALL_GRID));
    CHECK_EQUAL(ICON_SMALL_GRID, icon_width(ICON_PLAY, ICON_SMALL_GRID));
    CHECK_EQUAL(ICON_LARGE_GRID, icon_width(ICON_MUSIC, ICON_LARGE_GRID));
    CHECK_EQUAL(ICON_SMALL_GRID, icon_rows(ICON_SONG_SMALL));
    CHECK_EQUAL(ICON_LARGE_GRID, icon_rows(ICON_SONG));
}

static void an_icon_keeps_its_proportions_in_a_wider_or_taller_area(void)
{
    int side = ICON_LARGE_GRID;
    uint32_t square = drawn_hash(ICON_GAIN, (Rect){ORIGIN + side / 2, ORIGIN, side, side});
    CHECK_EQUAL(square, drawn_hash(ICON_GAIN, (Rect){ORIGIN, ORIGIN, 2 * side, side}));
    uint32_t lower = drawn_hash(ICON_GAIN, (Rect){ORIGIN, ORIGIN + side / 2, side, side});
    CHECK_EQUAL(lower, drawn_hash(ICON_GAIN, (Rect){ORIGIN, ORIGIN, side, 2 * side}));
    Rect slot = {ORIGIN, ORIGIN, ICON_NARROW_COLUMNS, ICON_SMALL_GRID};
    Rect wide = {ORIGIN - 2, ORIGIN, ICON_SMALL_GRID - 1, ICON_SMALL_GRID};
    CHECK_EQUAL(drawn_hash(ICON_LOCK, slot), drawn_hash(ICON_LOCK, wide));
}

static bool row_has_color(const Canvas *canvas, int y, Color color)
{
    for (int x = 0; x < CANVAS_SIZE; x++) {
        if (canvas_pixel_at(canvas, x, y) == color) {
            return true;
        }
    }
    return false;
}

static void the_strokes_of_an_icon_share_one_width_at_an_odd_size(void)
{
    Canvas canvas = blank_canvas();
    icon_draw(&canvas, ICON_KEY_MENU, (Rect){ORIGIN, ORIGIN, ODD_SIDE, ODD_SIDE}, INK);
    int run = 0;
    int bars = 0;
    for (int y = 0; y < CANVAS_SIZE; y++) {
        bool inked = row_has_color(&canvas, y, ACCENT);
        if (!inked && run > 0) {
            CHECK_EQUAL(ODD_STROKE, run);
            bars++;
        }
        run = inked ? run + 1 : 0;
    }
    CHECK_EQUAL(MENU_BARS, bars);
}

static void an_icon_scales_to_fill_a_larger_area(void)
{
    Canvas canvas = blank_canvas();
    icon_draw(&canvas, ICON_PAUSE, natural_area(ICON_PAUSE), (IconInk){MAIN, MAIN});
    int small = count_color(&canvas, MAIN);
    canvas = blank_canvas();
    Rect doubled = area_of_height(ICON_PAUSE, 2 * ICON_SMALL_GRID);
    icon_draw(&canvas, ICON_PAUSE, doubled, (IconInk){MAIN, MAIN});
    CHECK_EQUAL(4 * small, count_color(&canvas, MAIN));
}

static void an_icon_stays_inside_the_canvas_clip(void)
{
    Canvas canvas = blank_canvas();
    Rect area = natural_area(ICON_USB);
    Rect half = {ORIGIN, ORIGIN, ICON_LARGE_GRID / 2, ICON_LARGE_GRID};
    Canvas clipped = canvas_clipped(&canvas, half);
    icon_draw(&clipped, ICON_USB, area, INK);
    CHECK(count_ink(&canvas, half, true) > 0);
    CHECK_EQUAL(0, count_ink(&canvas, half, false));
}

static void enlarged_diagonals_use_the_destination_resolution(void)
{
    Canvas canvas = blank_canvas();
    Rect area = {ORIGIN, ORIGIN, 2 * ICON_LARGE_GRID, 2 * ICON_LARGE_GRID};
    icon_draw(&canvas, ICON_SHUFFLE, area, (IconInk){MAIN, MAIN});
    CHECK_EQUAL(MAIN, canvas_pixel_at(&canvas, ORIGIN + 12, ORIGIN + 9));
    CHECK_EQUAL(BACKGROUND, canvas_pixel_at(&canvas, ORIGIN + 13, ORIGIN + 9));
    CHECK_EQUAL(0, count_ink(&canvas, area, false));
}

static void accent_parts_use_the_accent_ink(void)
{
    Canvas canvas = blank_canvas();
    Rect area = natural_area(ICON_NOW_PLAYING);
    icon_draw(&canvas, ICON_NOW_PLAYING, area, INK);
    CHECK(count_color(&canvas, MAIN) > 0);
    CHECK(count_color(&canvas, ACCENT) > 0);
    canvas = blank_canvas();
    icon_draw(&canvas, ICON_NOW_PLAYING, area, (IconInk){MAIN, MAIN});
    CHECK_EQUAL(0, count_color(&canvas, ACCENT));
}

static void no_icon_draws_nothing(void)
{
    Canvas canvas = blank_canvas();
    Rect area = natural_area(ICON_MUSIC);
    icon_draw(&canvas, ICON_NONE, area, INK);
    icon_draw(&canvas, ICON_COUNT, area, INK);
    CHECK_EQUAL(0, count_ink(&canvas, area, true));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_icon_is_whole_at_each_text_size_in_both_orientations),
        TEST_CASE(a_large_icon_fits_a_list_row_at_every_text_size),
        TEST_CASE(status_icons_are_drawn_at_a_whole_multiple_of_their_grid),
        TEST_CASE(every_icon_stays_inside_its_area_at_any_size),
        TEST_CASE(symmetric_icons_stay_symmetric_at_whole_multiples_of_their_grid),
        TEST_CASE(every_icon_keeps_its_drawn_shape),
        TEST_CASE(a_narrow_icon_is_nine_columns_wide_for_fourteen_rows),
        TEST_CASE(an_icon_keeps_its_proportions_in_a_wider_or_taller_area),
        TEST_CASE(the_strokes_of_an_icon_share_one_width_at_an_odd_size),
        TEST_CASE(an_icon_scales_to_fill_a_larger_area),
        TEST_CASE(an_icon_stays_inside_the_canvas_clip),
        TEST_CASE(enlarged_diagonals_use_the_destination_resolution),
        TEST_CASE(accent_parts_use_the_accent_ink),
        TEST_CASE(no_icon_draws_nothing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
