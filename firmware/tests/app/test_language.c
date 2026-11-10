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

static uint8_t packs[PACKS][TEST_PACK_CAPACITY];
static uint32_t sizes[PACKS];

static void very_long(const char *english, char *text, size_t capacity)
{
    snprintf(text, capacity, "%s %0200d", english, 0);
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
    CHECK(strcmp("English (mine)", i18n_text(STR_LANGUAGE_NAME)) == 0);
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
    packs[2][sizes[2] - 5] ^= 1;
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
    fake_hal_add_language_file("qps.pack", packs[2], I18N_HEADER_BYTES - 1);
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
        TEST_CASE(the_list_holds_at_most_eight_languages),
        TEST_CASE(with_the_first_copy_unreadable_the_list_comes_from_the_second),
        TEST_CASE(with_nothing_to_list_the_list_is_empty),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
