#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "core/i18n.h"
#include "test_pack.h"

static uint8_t pack[TEST_PACK_CAPACITY];
static uint32_t pack_size;

static void build_bracketed(void)
{
    pack_size = test_pack_build(pack, (TestPackLanguage){"qps", "[Pseudo]", test_pack_bracketed});
}

static bool attaches_after(uint32_t offset, uint8_t value, bool sealed)
{
    build_bracketed();
    pack[offset] = value;
    if (sealed) {
        test_pack_seal(pack, pack_size);
    }
    return i18n_attach(pack, pack_size);
}

static void a_sound_pack_is_attached_and_gives_its_text(void)
{
    build_bracketed();
    CHECK(i18n_attach(pack, pack_size));
    CHECK(i18n_attached());
    CHECK(strcmp("[Settings]", i18n_text(STR_SETTINGS)) == 0);
    CHECK(strcmp("[Update not installed]", i18n_text(STR_UPDATE_NOT_INSTALLED)) == 0);
    i18n_detach();
}

static void the_last_string_of_the_catalogue_is_reachable(void)
{
    build_bracketed();
    CHECK(i18n_attach(pack, pack_size));
    const char *last = i18n_text((StringId)(STRING_COUNT - 1));
    CHECK_EQUAL('[', last[0]);
    CHECK_EQUAL(']', last[strlen(last) - 1]);
    CHECK(last + strlen(last) + 1 == (const char *)pack + pack_size);
    i18n_detach();
}

static void a_pack_names_its_language_and_code(void)
{
    build_bracketed();
    I18nPackInfo info;
    CHECK(i18n_pack_info(pack, I18N_HEADER_BYTES, &info));
    CHECK_EQUAL(pack_size, info.size);
    CHECK(strcmp("qps", info.code) == 0);
    CHECK(strcmp("[Pseudo]", info.name) == 0);
    CHECK(i18n_attach(pack, pack_size));
    CHECK(strcmp("qps", i18n_code()) == 0);
    CHECK(strcmp("[Pseudo]", i18n_text(STR_LANGUAGE_NAME)) == 0);
    i18n_detach();
    CHECK(strcmp("", i18n_code()) == 0);
}

static void a_header_shorter_than_the_format_is_refused(void)
{
    build_bracketed();
    I18nPackInfo info;
    CHECK(!i18n_pack_info(pack, I18N_HEADER_BYTES - 1, &info));
    CHECK(!i18n_pack_info(NULL, I18N_HEADER_BYTES, &info));
    CHECK(!i18n_attach(NULL, pack_size));
    CHECK(!i18n_attach(pack, I18N_HEADER_BYTES - 1));
}

static void another_magic_version_or_count_is_refused(void)
{
    CHECK(!attaches_after(0, 'E', true));
    CHECK(!attaches_after(TEST_PACK_VERSION_FIELD, I18N_VERSION + 1, true));
    CHECK(!attaches_after(TEST_PACK_COUNT_FIELD, (uint8_t)(STRING_COUNT + 1), true));
    CHECK(attaches_after(TEST_PACK_VERSION_FIELD, I18N_VERSION, true));
    i18n_detach();
}

static void a_pack_built_for_another_catalogue_is_refused(void)
{
    build_bracketed();
    hash_store(pack + TEST_PACK_CATALOGUE_FIELD, STRINGS_HASH ^ 1u);
    test_pack_seal(pack, pack_size);
    CHECK(!i18n_attach(pack, pack_size));
}

static void a_pack_longer_than_its_bytes_is_refused(void)
{
    build_bracketed();
    CHECK(!i18n_attach(pack, pack_size - 1));
    CHECK(i18n_attach(pack, pack_size + 100));
    i18n_detach();
}

static void a_pack_too_short_to_hold_any_text_is_refused(void)
{
    build_bracketed();
    hash_store(pack + TEST_PACK_SIZE_FIELD, TEST_PACK_TEXT_BASE);
    test_pack_seal(pack, TEST_PACK_TEXT_BASE);
    CHECK(!i18n_attach(pack, pack_size));
}

static void a_changed_byte_fails_the_checksum(void)
{
    uint8_t changed = (uint8_t)(pack[TEST_PACK_TEXT_BASE + 3] ^ 0x20u);
    CHECK(!attaches_after(TEST_PACK_TEXT_BASE + 3, changed, false));
    CHECK(!attaches_after(TEST_PACK_NAME_FIELD, 'X', false));
    CHECK(!attaches_after(I18N_HEADER_BYTES + 4, 9, false));
}

static void an_offset_outside_the_pack_is_refused(void)
{
    build_bracketed();
    uint32_t beyond = pack_size - TEST_PACK_TEXT_BASE;
    pack[I18N_HEADER_BYTES + 2 * STR_SETTINGS] = (uint8_t)beyond;
    pack[I18N_HEADER_BYTES + 2 * STR_SETTINGS + 1] = (uint8_t)(beyond >> 8);
    test_pack_seal(pack, pack_size);
    CHECK(!i18n_attach(pack, pack_size));
}

static void a_last_string_without_its_end_is_refused(void)
{
    CHECK(!attaches_after(pack_size - 1, '!', true));
}

static void a_code_or_name_without_its_end_is_refused(void)
{
    CHECK(!attaches_after(TEST_PACK_CODE_FIELD + I18N_CODE_BYTES - 1, 'x', true));
    CHECK(!attaches_after(TEST_PACK_NAME_FIELD + I18N_NAME_BYTES - 1, 'x', true));
}

static void a_refused_pack_leaves_nothing_attached(void)
{
    build_bracketed();
    CHECK(i18n_attach(pack, pack_size));
    pack[0] = 'X';
    CHECK(!i18n_attach(pack, pack_size));
    CHECK(!i18n_attached());
}

static void without_a_pack_a_host_build_shows_the_built_in_english(void)
{
    i18n_detach();
    CHECK(strcmp("Settings", i18n_text(STR_SETTINGS)) == 0);
    CHECK(strcmp("English", i18n_text(STR_LANGUAGE_NAME)) == 0);
}

static void an_id_outside_the_catalogue_gives_empty_text(void)
{
    CHECK(strcmp("", i18n_text(STRING_COUNT)) == 0);
    CHECK(strcmp("", i18n_text((StringId)(STRING_COUNT + 500))) == 0);
}

static const char *formatted(StringId pattern, const char *const *arguments, uint32_t count)
{
    static char text[96];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format(&builder, pattern, arguments, count);
    return text;
}

static void placeholders_take_their_arguments(void)
{
    const char *arguments[] = {"3", "12"};
    i18n_detach();
    CHECK(strcmp("Chapter 3 of 12", formatted(STR_CHAPTER_OF, arguments, 2)) == 0);
    CHECK(strcmp("Connected to Buds", formatted(STR_BT_CONNECTED_TO, &(const char *){"Buds"}, 1)) ==
          0);
}

static void swap_places(const char *english, char *text, size_t capacity)
{
    if (strcmp(english, "Chapter {0} of {1}") == 0) {
        strncpy(text, "{1} in all, now {0}", capacity);
    }
}

static void a_translation_may_reorder_its_placeholders(void)
{
    const char *arguments[] = {"3", "12"};
    pack_size = test_pack_build(pack, (TestPackLanguage){"xx", "Swapped", swap_places});
    CHECK(i18n_attach(pack, pack_size));
    CHECK(strcmp("12 in all, now 3", formatted(STR_CHAPTER_OF, arguments, 2)) == 0);
    i18n_detach();
}

static void a_placeholder_without_an_argument_is_left_out(void)
{
    const char *arguments[] = {"3"};
    i18n_detach();
    CHECK(strcmp("Chapter 3 of ", formatted(STR_CHAPTER_OF, arguments, 1)) == 0);
    CHECK(strcmp("Chapter  of ", formatted(STR_CHAPTER_OF, NULL, 0)) == 0);
}

static void odd_braces(const char *english, char *text, size_t capacity)
{
    if (strcmp(english, "{0} left") == 0) {
        strncpy(text, "{x} {12} {0 } { {0}", capacity);
    }
}

static void braces_that_are_not_placeholders_stay_as_text(void)
{
    pack_size = test_pack_build(pack, (TestPackLanguage){"xx", "Braces", odd_braces});
    CHECK(i18n_attach(pack, pack_size));
    CHECK(strcmp("{x} {12} {0 } { 4:05", formatted(STR_TIME_LEFT, &(const char *){"4:05"}, 1)) ==
          0);
    i18n_detach();
}

static void a_formatted_text_never_overruns_its_buffer(void)
{
    char text[8];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_detach();
    i18n_format_text(&builder, STR_BT_CONNECTED_TO, "a very long device name");
    CHECK(builder.overflowed);
    CHECK(strcmp("Connect", text) == 0);
}

static void counts_are_written_as_decimal_numbers(void)
{
    char text[48];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_detach();
    i18n_format_counts(&builder, STR_COVERS_DONE, 7, 4000000000u);
    CHECK(strcmp("7 of 4000000000 albums", text) == 0);
    builder = string_builder_make(text, sizeof text);
    i18n_format_counts(&builder, STR_SCAN_TRACKS, 1563, 0);
    CHECK(strcmp("1563 tracks so far", text) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_sound_pack_is_attached_and_gives_its_text),
        TEST_CASE(the_last_string_of_the_catalogue_is_reachable),
        TEST_CASE(a_pack_names_its_language_and_code),
        TEST_CASE(a_header_shorter_than_the_format_is_refused),
        TEST_CASE(another_magic_version_or_count_is_refused),
        TEST_CASE(a_pack_built_for_another_catalogue_is_refused),
        TEST_CASE(a_pack_longer_than_its_bytes_is_refused),
        TEST_CASE(a_pack_too_short_to_hold_any_text_is_refused),
        TEST_CASE(a_changed_byte_fails_the_checksum),
        TEST_CASE(an_offset_outside_the_pack_is_refused),
        TEST_CASE(a_last_string_without_its_end_is_refused),
        TEST_CASE(a_code_or_name_without_its_end_is_refused),
        TEST_CASE(a_refused_pack_leaves_nothing_attached),
        TEST_CASE(without_a_pack_a_host_build_shows_the_built_in_english),
        TEST_CASE(an_id_outside_the_catalogue_gives_empty_text),
        TEST_CASE(placeholders_take_their_arguments),
        TEST_CASE(a_translation_may_reorder_its_placeholders),
        TEST_CASE(a_placeholder_without_an_argument_is_left_out),
        TEST_CASE(braces_that_are_not_placeholders_stay_as_text),
        TEST_CASE(a_formatted_text_never_overruns_its_buffer),
        TEST_CASE(counts_are_written_as_decimal_numbers),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
