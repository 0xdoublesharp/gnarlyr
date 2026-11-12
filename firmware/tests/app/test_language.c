#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "fake_hal.h"
#include "language.h"
#include "test_pack.h"

enum {
    PACKS = 10
};

static const uint8_t MODULES[STRING_SEGMENT_COUNT] = STRING_SEGMENT_MODULES;
static const char QPS_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/lang/qps.pack";

static uint8_t packs[PACKS][TEST_PACK_CAPACITY];
static uint32_t sizes[PACKS];

static void very_long(const char *english, char *text, size_t capacity)
{
    snprintf(text, capacity, "%s %060d", english, 0);
}

static const uint8_t *make(int slot, const char *code, const char *name, TestPackText translate)
{
    sizes[slot] = test_pack_build(packs[slot], (TestPackLanguage){code, name, translate});
    return packs[slot];
}

static void start(void)
{
    fake_hal_reset();
    i18n_detach();
}

static void built_in(int copy, int slot)
{
    fake_hal_add_language_pack(copy, packs[slot], sizes[slot]);
}

static void standard_image(void)
{
    start();
    make(0, "en", "English", NULL);
    make(1, "up", "UPPER", test_pack_upper_case);
    for (int copy = 0; copy < 2; copy++) {
        built_in(copy, 0);
        built_in(copy, 1);
    }
}

static bool shows(const char *expected)
{
    return strcmp(expected, i18n_text(STR_SETTINGS)) == 0;
}

static StringId first_of(uint32_t segment)
{
    for (uint32_t id = 0; id < STRING_COUNT; id++) {
        if (test_pack_segment_of((StringId)id) == segment) {
            return (StringId)id;
        }
    }
    return STRING_COUNT;
}

static const char *module_text(StringId id)
{
    language_use_module(MODULES[test_pack_segment_of(id)]);
    return i18n_text(id);
}

static bool bracketed(const char *text)
{
    return text[0] == '[' && text[strlen(text) - 1] == ']';
}

static bool english(StringId id)
{
    static char kept[TEST_PACK_TEXT_CAPACITY];
    snprintf(kept, sizeof kept, "%s", i18n_text(id));
    i18n_detach_segment();
    bool same = strcmp(kept, i18n_text(id)) == 0;
    return same;
}

static void pseudo_from_a_file(void)
{
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    CHECK(language_load("qps"));
}

static void with_no_language_chosen_the_first_pack_of_the_image_is_loaded(void)
{
    standard_image();
    CHECK(language_load(""));
    CHECK(language_is("en"));
    CHECK(i18n_attached() && shows("Settings"));
}

static void a_chosen_language_of_the_image_is_loaded(void)
{
    standard_image();
    CHECK(language_load("up"));
    CHECK(language_is("up") && shows("SETTINGS"));
}

static void the_first_pack_of_the_image_is_the_default_whatever_its_language(void)
{
    start();
    make(0, "up", "UPPER", test_pack_upper_case);
    make(1, "en", "English", NULL);
    built_in(0, 0);
    built_in(0, 1);
    CHECK(language_load(""));
    CHECK(language_is("up"));
}

static void a_chosen_language_that_is_missing_falls_back_to_the_default(void)
{
    standard_image();
    CHECK(language_load("fr"));
    CHECK(language_is("en") && shows("Settings"));
    CHECK(!language_is("fr"));
}

static void a_file_in_the_data_folder_adds_a_language(void)
{
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    CHECK(language_load("qps"));
    CHECK(language_is("qps") && shows("[Settings]"));
}

static void a_file_in_the_data_folder_overrides_a_language_of_the_image(void)
{
    standard_image();
    make(2, "en", "English (mine)", test_pack_bracketed);
    fake_hal_add_language_file("en.pack", packs[2], sizes[2]);
    CHECK(language_load("en"));
    CHECK(shows("[Settings]"));
    CHECK(strcmp("English (mine)", module_text(STR_LANGUAGE_NAME)) == 0);
}

static void a_file_is_only_used_for_the_chosen_language(void)
{
    standard_image();
    make(2, "en", "English (mine)", test_pack_bracketed);
    fake_hal_add_language_file("en.pack", packs[2], sizes[2]);
    CHECK(language_load(""));
    CHECK(shows("Settings"));
}

static void a_file_with_a_changed_byte_falls_back_to_the_image(void)
{
    standard_image();
    make(2, "up", "UPPER (mine)", test_pack_bracketed);
    packs[2][I18N_INDEX_BYTES + 2 * (uint32_t)first_of(1) + 1] ^= 1;
    fake_hal_add_language_file("up.pack", packs[2], sizes[2]);
    CHECK(language_load("up"));
    CHECK(language_is("up") && shows("SETTINGS"));
}

static void a_damaged_file_for_a_language_the_image_lacks_falls_back_to_the_default(void)
{
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    packs[2][TEST_PACK_TEXT_BASE + 2] ^= 1;
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    CHECK(language_load("qps"));
    CHECK(language_is("en") && shows("Settings"));
}

static void a_file_built_for_another_catalogue_is_ignored(void)
{
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    hash_store(packs[2] + TEST_PACK_CATALOGUE_FIELD, STRINGS_HASH + 1u);
    test_pack_seal(packs[2], sizes[2]);
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    CHECK(language_load("qps"));
    CHECK(language_is("en"));
}

static void a_file_cut_short_is_ignored(void)
{
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2] - 100);
    CHECK(language_load("qps"));
    CHECK(language_is("en") && shows("Settings"));
    start();
    built_in(0, 0);
    fake_hal_add_language_file("qps.pack", packs[2], I18N_INDEX_BYTES - 1);
    CHECK(language_load("qps"));
    CHECK(language_is("en"));
}

static void a_file_larger_than_the_pack_memory_is_ignored(void)
{
    standard_image();
    CHECK(language_load("up"));
    make(2, "qps", "[Pseudo]", very_long);
    CHECK(sizes[2] > I18N_PACK_CAPACITY);
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    int reads = fake_hal_language_read_count();
    CHECK(language_load("qps"));
    CHECK(language_is("en") && shows("Settings"));
    CHECK(fake_hal_language_read_count() > reads);
}

static void a_file_named_for_another_language_than_it_holds_is_ignored(void)
{
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    fake_hal_add_language_file("fr.pack", packs[2], sizes[2]);
    CHECK(language_load("fr"));
    CHECK(language_is("en") && shows("Settings"));
}

static void a_damaged_first_copy_is_replaced_by_the_second(void)
{
    start();
    make(0, "en", "English", NULL);
    make(1, "en", "English", NULL);
    packs[0][sizes[0] - 3] ^= 0x40;
    built_in(0, 0);
    built_in(1, 1);
    CHECK(language_load(""));
    CHECK(language_is("en") && shows("Settings"));
    CHECK(i18n_attached());
}

static void a_first_copy_from_another_build_is_replaced_by_the_second(void)
{
    start();
    make(0, "en", "English", test_pack_bracketed);
    hash_store(packs[0] + TEST_PACK_CATALOGUE_FIELD, STRINGS_HASH ^ 0x55u);
    test_pack_seal(packs[0], sizes[0]);
    make(1, "en", "English", NULL);
    make(2, "up", "UPPER", test_pack_upper_case);
    built_in(0, 0);
    built_in(1, 1);
    built_in(1, 2);
    CHECK(language_load("up"));
    CHECK(language_is("up") && shows("SETTINGS"));
}

static void a_language_missing_from_the_first_copy_comes_from_the_second(void)
{
    start();
    make(0, "en", "English", NULL);
    make(1, "up", "UPPER", test_pack_upper_case);
    built_in(0, 0);
    built_in(1, 0);
    built_in(1, 1);
    CHECK(language_load("up"));
    CHECK(language_is("up"));
}

static void with_no_sound_pack_anywhere_nothing_is_attached(void)
{
    start();
    make(0, "en", "English", NULL);
    packs[0][0] = 'X';
    built_in(0, 0);
    built_in(1, 0);
    CHECK(!language_load(""));
    CHECK(!i18n_attached());
    CHECK(!language_load("en"));
    start();
    CHECK(!language_load(""));
}

static void a_pack_that_cannot_be_replaced_stays_loaded(void)
{
    standard_image();
    CHECK(language_load("up"));
    fake_hal_reset();
    CHECK(language_load("en"));
    CHECK(language_is("up") && shows("SETTINGS"));
}

static void while_an_update_waits_for_a_restart_the_loaded_pack_is_kept(void)
{
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    CHECK(language_load("up"));
    fake_hal_set_restart_pending(true);
    int reads = fake_hal_language_read_count();
    CHECK(language_load("qps"));
    CHECK(language_is("up") && shows("SETTINGS"));
    CHECK(language_load(""));
    CHECK(language_is("up"));
    CHECK_EQUAL(reads, fake_hal_language_read_count());
}

static void before_any_pack_was_loaded_a_pending_restart_leaves_none(void)
{
    standard_image();
    fake_hal_set_restart_pending(true);
    CHECK(!language_load(""));
    CHECK(!i18n_attached());
}

static void the_list_names_the_packs_of_the_image_in_order(void)
{
    LanguageList list;
    standard_image();
    language_list(&list);
    CHECK_EQUAL(2, list.count);
    CHECK(strcmp("en", list.codes[0]) == 0 && strcmp("English", list.options[0]) == 0);
    CHECK(strcmp("up", list.codes[1]) == 0 && strcmp("UPPER", list.options[1]) == 0);
    CHECK_EQUAL(1, language_list_find(&list, "up"));
    CHECK_EQUAL(-1, language_list_find(&list, "fr"));
    CHECK_EQUAL(-1, language_list_find(&list, ""));
}

static void listing_does_not_disturb_the_loaded_pack(void)
{
    LanguageList list;
    standard_image();
    CHECK(language_load("up"));
    language_list(&list);
    CHECK(language_is("up") && shows("SETTINGS"));
}

static void files_are_listed_after_the_packs_of_the_image(void)
{
    LanguageList list;
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    language_list(&list);
    CHECK_EQUAL(3, list.count);
    CHECK(strcmp("qps", list.codes[2]) == 0 && strcmp("[Pseudo]", list.options[2]) == 0);
}

static void a_file_that_overrides_a_language_of_the_image_is_listed_once_under_its_own_name(void)
{
    LanguageList list;
    standard_image();
    make(2, "up", "UPPER (mine)", test_pack_bracketed);
    fake_hal_add_language_file("up.pack", packs[2], sizes[2]);
    language_list(&list);
    CHECK_EQUAL(2, list.count);
    CHECK(strcmp("UPPER (mine)", list.options[1]) == 0);
}

static void files_that_are_not_packs_of_this_catalogue_are_not_listed(void)
{
    LanguageList list;
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    make(3, "old", "Old", NULL);
    hash_store(packs[3] + TEST_PACK_CATALOGUE_FIELD, STRINGS_HASH + 7u);
    fake_hal_add_language_file("qps.txt", packs[2], sizes[2]);
    fake_hal_add_language_file("._qps.pack", packs[2], sizes[2]);
    fake_hal_add_language_file("fr.pack", packs[2], sizes[2]);
    fake_hal_add_language_file("old.pack", packs[3], sizes[3]);
    fake_hal_add_language_file("qps.pack.bak", packs[2], sizes[2]);
    fake_hal_add_language_file("verylongname.pack", packs[2], sizes[2]);
    fake_hal_add_language_file("tiny.pack", packs[2], 10);
    language_list(&list);
    CHECK_EQUAL(2, list.count);
}

static void a_file_with_a_damaged_or_oversized_module_segment_is_not_listed_or_chosen(void)
{
    LanguageList list;
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    make(3, "zz", "Zed", test_pack_bracketed);
    I18nSegment row = test_pack_segment(packs[2], STRING_SEGMENT_COUNT - 1);
    packs[2][row.offset + row.size - 2] ^= 0x01;
    hash_store(packs[3] + TEST_PACK_ROWS_FIELD + I18N_SEGMENT_BYTES + 8, I18N_PACK_CAPACITY);
    test_pack_seal(packs[3], sizes[3]);
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    fake_hal_add_language_file("zz.pack", packs[3], sizes[3]);
    language_list(&list);
    CHECK_EQUAL(2, list.count);
    CHECK(!language_file_sound("qps"));
    CHECK(!language_file_sound("zz"));
    CHECK(language_file_sound("up"));
    packs[2][row.offset + row.size - 2] ^= 0x01;
    CHECK(language_file_sound("qps"));
}

static void the_list_holds_at_most_eight_languages(void)
{
    static const char *const CODES[] = {"a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9"};
    static char names[9][16];
    LanguageList list;
    start();
    make(0, "en", "English", NULL);
    built_in(0, 0);
    for (int index = 0; index < 9; index++) {
        make(index + 1, CODES[index], CODES[index], NULL);
        snprintf(names[index], sizeof names[index], "%s.pack", CODES[index]);
        fake_hal_add_language_file(names[index], packs[index + 1], sizes[index + 1]);
    }
    language_list(&list);
    CHECK_EQUAL(LANGUAGE_CAPACITY, list.count);
    CHECK(strcmp("en", list.codes[0]) == 0);
    CHECK(strcmp("a7", list.codes[7]) == 0);
}

static void with_the_first_copy_unreadable_the_list_comes_from_the_second(void)
{
    LanguageList list;
    start();
    make(0, "en", "English", NULL);
    make(1, "up", "UPPER", test_pack_upper_case);
    built_in(1, 0);
    built_in(1, 1);
    language_list(&list);
    CHECK_EQUAL(2, list.count);
}

static void with_nothing_to_list_the_list_is_empty(void)
{
    LanguageList list;
    start();
    language_list(&list);
    CHECK_EQUAL(0, list.count);
}

static void each_module_shows_its_own_segment_of_the_chosen_language(void)
{
    pseudo_from_a_file();
    for (uint32_t segment = 1; segment < STRING_SEGMENT_COUNT; segment++) {
        CHECK(language_use_module(MODULES[segment]));
        CHECK_EQUAL(segment, i18n_segment_loaded());
        CHECK(bracketed(i18n_text(first_of(segment))));
        CHECK(bracketed(i18n_text(STR_SETTINGS)));
        uint32_t other = segment == 1 ? 2 : 1;
        CHECK(!bracketed(i18n_text(first_of(other))));
    }
}

static void switching_modules_reloads_the_right_segment_and_staying_reads_nothing(void)
{
    standard_image();
    CHECK(language_load("up"));
    CHECK(language_use_module(MODULES[1]));
    int reads = fake_hal_language_read_count();
    CHECK(!language_use_module(MODULES[1]));
    CHECK_EQUAL(reads, fake_hal_language_read_count());
    CHECK(language_use_module(MODULES[2]));
    CHECK(fake_hal_language_read_count() > reads);
    CHECK_EQUAL(2, i18n_segment_loaded());
    CHECK(!language_use_module(MODULES[1]));
    CHECK_EQUAL(1, i18n_segment_loaded());
    CHECK(!language_use_module(0));
    CHECK_EQUAL(1, i18n_segment_loaded());
}

static void a_module_whose_pack_file_is_gone_shows_english_until_the_file_returns(void)
{
    pseudo_from_a_file();
    CHECK(language_use_module(MODULES[1]));
    CHECK(bracketed(i18n_text(first_of(1))));
    fake_hal_fail_file_opens(QPS_PATH);
    CHECK(language_use_module(MODULES[2]));
    CHECK_EQUAL(2, i18n_segment_loaded());
    CHECK(!bracketed(i18n_text(first_of(2))));
    CHECK(english(first_of(2)));
    CHECK(bracketed(i18n_text(STR_SETTINGS)));
    CHECK(language_is("qps"));
    fake_hal_fail_file_opens(NULL);
    CHECK(!language_use_module(MODULES[1]));
    CHECK(bracketed(i18n_text(first_of(1))));
    CHECK(language_use_module(MODULES[2]));
    CHECK(bracketed(i18n_text(first_of(2))));
}

static void a_module_segment_damaged_after_loading_falls_back_to_the_default_pack(void)
{
    standard_image();
    CHECK(language_load("up"));
    I18nSegment row = test_pack_segment(packs[1], 2);
    packs[1][row.offset + row.size - 2] ^= 0x01;
    CHECK(language_use_module(MODULES[2]));
    CHECK_EQUAL(2, i18n_segment_loaded());
    CHECK(english(first_of(2)));
    CHECK(shows("SETTINGS"));
}

static void with_no_readable_segment_a_module_falls_back_to_built_in_text(void)
{
    standard_image();
    CHECK(language_load("up"));
    CHECK(language_use_module(MODULES[2]));
    CHECK(language_use_module(MODULES[1]));
    for (int slot = 0; slot < 2; slot++) {
        I18nSegment row = test_pack_segment(packs[slot], 2);
        packs[slot][row.offset + 1] ^= 0x01;
    }
    CHECK(language_use_module(MODULES[2]));
    CHECK_EQUAL(0, i18n_segment_loaded());
    CHECK(english(first_of(2)));
    CHECK(!language_use_module(MODULES[2]));
}

static void a_file_with_a_damaged_module_segment_shows_that_module_in_english(void)
{
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    I18nSegment row = test_pack_segment(packs[2], STRING_SEGMENT_COUNT - 1);
    packs[2][row.offset + row.size - 2] ^= 0x01;
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    CHECK(language_load("qps"));
    CHECK(language_is("qps") && shows("[Settings]"));
    CHECK(language_use_module(MODULES[1]));
    CHECK(bracketed(i18n_text(first_of(1))));
    CHECK(language_use_module(MODULES[STRING_SEGMENT_COUNT - 1]));
    CHECK_EQUAL(STRING_SEGMENT_COUNT - 1, i18n_segment_loaded());
    CHECK(english(first_of(STRING_SEGMENT_COUNT - 1)));
}

static void a_module_segment_too_large_for_the_window_shows_that_module_in_english(void)
{
    standard_image();
    make(2, "qps", "[Pseudo]", test_pack_bracketed);
    I18nSegment resident = test_pack_segment(packs[2], 0);
    uint32_t room = I18N_PACK_CAPACITY - (resident.offset + resident.size + 3) / 4 * 4;
    hash_store(packs[2] + TEST_PACK_ROWS_FIELD + I18N_SEGMENT_BYTES + 8, room + 1);
    test_pack_seal(packs[2], sizes[2]);
    fake_hal_add_language_file("qps.pack", packs[2], sizes[2]);
    CHECK(language_load("qps"));
    language_use_module(MODULES[1]);
    CHECK_EQUAL(1, i18n_segment_loaded());
    CHECK(english(first_of(1)));
}

static void a_module_number_without_a_segment_reads_nothing(void)
{
    standard_image();
    CHECK(language_load("up"));
    int reads = fake_hal_language_read_count();
    CHECK(!language_use_module(0));
    CHECK(!language_use_module(200));
    CHECK_EQUAL(reads, fake_hal_language_read_count());
    CHECK_EQUAL(0, i18n_segment_loaded());
}

static void while_an_update_waits_for_a_restart_module_text_from_the_image_falls_back(void)
{
    standard_image();
    CHECK(language_load("up"));
    fake_hal_set_restart_pending(true);
    language_use_module(MODULES[1]);
    CHECK_EQUAL(0, i18n_segment_loaded());
    CHECK(english(first_of(1)));
    CHECK(shows("SETTINGS"));
    fake_hal_set_restart_pending(false);
}

static void without_a_pack_no_module_text_is_read(void)
{
    start();
    CHECK(!language_use_module(MODULES[1]));
    CHECK_EQUAL(0, fake_hal_language_read_count());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(with_no_language_chosen_the_first_pack_of_the_image_is_loaded),
        TEST_CASE(a_chosen_language_of_the_image_is_loaded),
        TEST_CASE(the_first_pack_of_the_image_is_the_default_whatever_its_language),
        TEST_CASE(a_chosen_language_that_is_missing_falls_back_to_the_default),
        TEST_CASE(a_file_in_the_data_folder_adds_a_language),
        TEST_CASE(a_file_in_the_data_folder_overrides_a_language_of_the_image),
        TEST_CASE(a_file_is_only_used_for_the_chosen_language),
        TEST_CASE(a_file_with_a_changed_byte_falls_back_to_the_image),
        TEST_CASE(a_damaged_file_for_a_language_the_image_lacks_falls_back_to_the_default),
        TEST_CASE(a_file_built_for_another_catalogue_is_ignored),
        TEST_CASE(a_file_cut_short_is_ignored),
        TEST_CASE(a_file_larger_than_the_pack_memory_is_ignored),
        TEST_CASE(a_file_named_for_another_language_than_it_holds_is_ignored),
        TEST_CASE(a_damaged_first_copy_is_replaced_by_the_second),
        TEST_CASE(a_first_copy_from_another_build_is_replaced_by_the_second),
        TEST_CASE(a_language_missing_from_the_first_copy_comes_from_the_second),
        TEST_CASE(with_no_sound_pack_anywhere_nothing_is_attached),
        TEST_CASE(a_pack_that_cannot_be_replaced_stays_loaded),
        TEST_CASE(while_an_update_waits_for_a_restart_the_loaded_pack_is_kept),
        TEST_CASE(before_any_pack_was_loaded_a_pending_restart_leaves_none),
        TEST_CASE(the_list_names_the_packs_of_the_image_in_order),
        TEST_CASE(listing_does_not_disturb_the_loaded_pack),
        TEST_CASE(files_are_listed_after_the_packs_of_the_image),
        TEST_CASE(a_file_that_overrides_a_language_of_the_image_is_listed_once_under_its_own_name),
        TEST_CASE(files_that_are_not_packs_of_this_catalogue_are_not_listed),
        TEST_CASE(a_file_with_a_damaged_or_oversized_module_segment_is_not_listed_or_chosen),
        TEST_CASE(the_list_holds_at_most_eight_languages),
        TEST_CASE(with_the_first_copy_unreadable_the_list_comes_from_the_second),
        TEST_CASE(each_module_shows_its_own_segment_of_the_chosen_language),
        TEST_CASE(switching_modules_reloads_the_right_segment_and_staying_reads_nothing),
        TEST_CASE(a_module_whose_pack_file_is_gone_shows_english_until_the_file_returns),
        TEST_CASE(a_module_segment_damaged_after_loading_falls_back_to_the_default_pack),
        TEST_CASE(with_no_readable_segment_a_module_falls_back_to_built_in_text),
        TEST_CASE(a_file_with_a_damaged_module_segment_shows_that_module_in_english),
        TEST_CASE(a_module_segment_too_large_for_the_window_shows_that_module_in_english),
        TEST_CASE(a_module_number_without_a_segment_reads_nothing),
        TEST_CASE(while_an_update_waits_for_a_restart_module_text_from_the_image_falls_back),
        TEST_CASE(without_a_pack_no_module_text_is_read),
        TEST_CASE(with_nothing_to_list_the_list_is_empty),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
