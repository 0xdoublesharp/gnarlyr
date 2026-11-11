#include <stdbool.h>
#include <stdint.h>

#include "check.h"
#include "fonts/fonts.h"
#include "ui/placeholder_art.h"

enum {
    HALF = 56,
    FULL = 112,
    SEEDS = 64,
    MIN_CONTRAST = 112
};

static const uint8_t WIDE_BITMAP[] = {0xFF, 0xFF};

static const Glyph WIDE_GLYPHS[] = {
    {'?', 0, 1, 1, 0, 1, 2},
    {0x4EAC, 0, 2, 2, 0, 2, 3},
    {0x6771, 0, 2, 2, 0, 2, 3},
};

static const Font WIDE_FONT = {
    .glyphs = WIDE_GLYPHS,
    .bitmap = WIDE_BITMAP,
    .glyph_count = 3,
    .bitmap_size = sizeof WIDE_BITMAP,
    .ascent = 2,
    .descent = 0,
};

static const ThemeColors DARK = {
    .background = COLOR_RGB(16, 17, 20),
    .text = COLOR_RGB(242, 242, 245),
    .selection_top = COLOR_RGB(66, 128, 232),
    .selection_shine = COLOR_RGB(220, 255, 231),
    .scrollbar_thumb = COLOR_RGB(96, 97, 106),
    .danger = COLOR_RGB(235, 77, 61),
    .success = COLOR_RGB(52, 199, 89),
    .accent = COLOR_RGB(96, 156, 244),
    .accent_alt = COLOR_RGB(150, 154, 170),
};

static const ThemeColors LIGHT = {
    .background = COLOR_RGB(236, 225, 199),
    .text = COLOR_RGB(33, 31, 27),
    .selection_top = COLOR_RGB(238, 182, 85),
    .selection_shine = COLOR_RGB(248, 210, 139),
    .scrollbar_thumb = COLOR_RGB(115, 95, 61),
    .danger = COLOR_RGB(149, 41, 35),
    .success = COLOR_RGB(46, 98, 59),
    .accent = COLOR_RGB(166, 88, 20),
    .accent_alt = COLOR_RGB(60, 98, 110),
};

static Color pixels[FULL * FULL];

static ArtInitials initials_of(const char *text)
{
    return placeholder_art_initials(&FONT_INTER_SEMIBOLD_26, text);
}

static void check_initials(const char *text, uint32_t first, uint32_t second)
{
    ArtInitials initials = initials_of(text);
    CHECK_EQUAL(second != 0 ? 2 : 1, initials.count);
    CHECK_EQUAL(first, initials.letters[0]);
    if (second != 0) {
        CHECK_EQUAL(second, initials.letters[1]);
    }
}

static int luma(Color color)
{
    int red = (color >> 11) & 0x1F;
    int green = (color >> 5) & 0x3F;
    int blue = color & 0x1F;
    return (red * 8 * 77 + green * 4 * 150 + blue * 8 * 29) >> 8;
}

static int contrast(Color first, Color second)
{
    int difference = luma(first) - luma(second);
    return difference < 0 ? -difference : difference;
}

static PlaceholderArt art_for(const char *text, const ThemeColors *colors)
{
    return (PlaceholderArt){
        .initials = initials_of(text),
        .ink = placeholder_art_ink(colors, 7),
        .fonts = {&FONT_INTER_SEMIBOLD_26, &FONT_INTER_SEMIBOLD_22},
    };
}

static Rect inked_area(const Canvas *canvas, Color ink)
{
    Rect tile = canvas->bounds;
    int left = tile.width, top = tile.height, right = -1, bottom = -1;
    for (int y = 0; y < tile.height; y++) {
        for (int x = 0; x < tile.width; x++) {
            if (canvas_pixel_at(canvas, x, y) == ink) {
                left = x < left ? x : left;
                right = x > right ? x : right;
                top = y < top ? y : top;
                bottom = y > bottom ? y : bottom;
            }
        }
    }
    return right < 0 ? (Rect){0, 0, 0, 0} : (Rect){left, top, right - left + 1, bottom - top + 1};
}

static Rect draw_tile(const char *text, int size)
{
    PlaceholderArt art = art_for(text, &DARK);
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, size, size});
    placeholder_art_draw(&canvas, &art);
    return inked_area(&canvas, art.ink.text);
}

static void initials_are_the_first_letters_of_the_first_two_words(void)
{
    check_initials("Back In Black", 'B', 'I');
    check_initials("  homogenic  ", 'H', 0);
}

static void a_leading_the_is_skipped_like_the_library_sort(void)
{
    check_initials("The Best of Something", 'B', 'O');
    check_initials("the  velvet underground", 'V', 'U');
    check_initials("The", 'T', 0);
    check_initials("Theory of Everything", 'T', 'O');
}

static void leading_punctuation_and_symbols_are_skipped(void)
{
    check_initials("(What's the Story) Morning Glory?", 'W', 'T');
    check_initials("...And Justice for All", 'A', 'J');
    check_initials("\xE2\x80\x9C"
                   "Heroes"
                   "\xE2\x80\x9D",
                   'H', 0);
    check_initials("It's a Long Way", 'I', 'A');
}

static void digits_count_as_initials(void)
{
    check_initials("1989", '1', 0);
    check_initials("Vol. 2", 'V', '2');
}

static void slashes_dashes_and_ampersands_separate_words(void)
{
    check_initials("AC/DC", 'A', 'D');
    check_initials("Jay-Z", 'J', 'Z');
    check_initials("Simon & Garfunkel", 'S', 'G');
}

static void lowercase_letters_are_shown_as_capitals(void)
{
    check_initials("\xC3\xA9lan vital", 0xC9, 'V');
    check_initials("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80",
                   0x41F, 0x41C);
}

static void a_syllabic_script_shows_one_character(void)
{
    ArtInitials initials = placeholder_art_initials(&WIDE_FONT, "\xE6\x9D\xB1\xE4\xBA\xAC");
    CHECK_EQUAL(1, initials.count);
    CHECK_EQUAL(0x6771, initials.letters[0]);
    initials = placeholder_art_initials(&WIDE_FONT, "\xE6\x9D\xB1 \xE4\xBA\xAC");
    CHECK_EQUAL(1, initials.count);
}

static void words_whose_initial_has_no_glyph_are_passed_over(void)
{
    check_initials("\xE6\x9D\xB1\xE4\xBA\xAC Nights", 'N', 0);
}

static void text_without_letters_or_digits_has_no_initials(void)
{
    CHECK_EQUAL(0, initials_of("").count);
    CHECK_EQUAL(0, initials_of("!!! ... ?").count);
    CHECK_EQUAL(0, initials_of("\xE6\x9D\xB1\xE4\xBA\xAC").count);
}

static void the_same_seed_always_gives_the_same_colours(void)
{
    ArtInk first = placeholder_art_ink(&DARK, 0x12345678);
    ArtInk second = placeholder_art_ink(&DARK, 0x12345678);
    CHECK_EQUAL(first.top, second.top);
    CHECK_EQUAL(first.bottom, second.bottom);
}

static void different_albums_spread_across_the_palette(void)
{
    Color seen[SEEDS];
    int distinct = 0;
    for (uint32_t seed = 0; seed < SEEDS; seed++) {
        Color top = placeholder_art_ink(&DARK, seed * 2654435761u).top;
        bool known = false;
        for (int index = 0; index < distinct; index++) {
            known = known || seen[index] == top;
        }
        if (!known) {
            seen[distinct++] = top;
        }
    }
    CHECK(distinct >= 6);
}

static void colours_come_from_the_theme_and_keep_its_text_readable(void)
{
    const ThemeColors *themes[] = {&DARK, &LIGHT};
    for (int theme = 0; theme < 2; theme++) {
        for (uint32_t seed = 0; seed < SEEDS; seed++) {
            ArtInk ink = placeholder_art_ink(themes[theme], seed * 40503u);
            CHECK_EQUAL(themes[theme]->text, ink.text);
            CHECK(ink.top != themes[theme]->background);
            CHECK(contrast(ink.top, ink.text) >= MIN_CONTRAST);
            CHECK(contrast(ink.bottom, ink.text) >= contrast(ink.top, ink.text));
        }
    }
}

static void a_tile_is_a_vertical_gradient_in_its_ink(void)
{
    PlaceholderArt art = art_for("Homogenic", &DARK);
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, HALF, HALF});
    placeholder_art_draw(&canvas, &art);
    CHECK_EQUAL(art.ink.top, canvas_pixel_at(&canvas, 0, 0));
    CHECK_EQUAL(art.ink.top, canvas_pixel_at(&canvas, HALF - 1, 0));
    CHECK_EQUAL(art.ink.bottom, canvas_pixel_at(&canvas, 0, HALF - 1));
}

static void initials_are_centred_in_the_tile(void)
{
    Rect ink = draw_tile("Back In Black", FULL);
    CHECK(ink.width > 0);
    int middle_x = 2 * ink.x + ink.width;
    int middle_y = 2 * ink.y + ink.height;
    CHECK(middle_x >= FULL - 4 && middle_x <= FULL + 4);
    CHECK(middle_y >= FULL - 4 && middle_y <= FULL + 4);
}

static void initials_grow_with_the_tile(void)
{
    Rect half = draw_tile("Back In Black", HALF);
    Rect full = draw_tile("Back In Black", FULL);
    CHECK(full.height >= 2 * half.height - 4);
}

static void wide_initials_stay_inside_the_margins_at_every_size(void)
{
    const int sizes[] = {HALF, FULL};
    for (int index = 0; index < 2; index++) {
        int size = sizes[index];
        Rect ink = draw_tile("Wide Mouth", size);
        CHECK(ink.width > 0);
        CHECK(ink.x >= size / 10);
        CHECK(ink.x + ink.width <= size - size / 10);
        CHECK(ink.height * 5 <= size * 2 + 5);
    }
}

static void without_initials_the_tile_shows_the_album_icon(void)
{
    Rect ink = draw_tile("!!!", FULL);
    CHECK(ink.width > 0);
    CHECK(ink.x >= FULL / 4 - 1 && ink.x + ink.width <= FULL * 3 / 4 + 1);
    CHECK(ink.y >= FULL / 4 - 1 && ink.y + ink.height <= FULL * 3 / 4 + 1);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(initials_are_the_first_letters_of_the_first_two_words),
        TEST_CASE(a_leading_the_is_skipped_like_the_library_sort),
        TEST_CASE(leading_punctuation_and_symbols_are_skipped),
        TEST_CASE(digits_count_as_initials),
        TEST_CASE(slashes_dashes_and_ampersands_separate_words),
        TEST_CASE(lowercase_letters_are_shown_as_capitals),
        TEST_CASE(a_syllabic_script_shows_one_character),
        TEST_CASE(words_whose_initial_has_no_glyph_are_passed_over),
        TEST_CASE(text_without_letters_or_digits_has_no_initials),
        TEST_CASE(the_same_seed_always_gives_the_same_colours),
        TEST_CASE(different_albums_spread_across_the_palette),
        TEST_CASE(colours_come_from_the_theme_and_keep_its_text_readable),
        TEST_CASE(a_tile_is_a_vertical_gradient_in_its_ink),
        TEST_CASE(initials_are_centred_in_the_tile),
        TEST_CASE(initials_grow_with_the_tile),
        TEST_CASE(wide_initials_stay_inside_the_margins_at_every_size),
        TEST_CASE(without_initials_the_tile_shows_the_album_icon),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
