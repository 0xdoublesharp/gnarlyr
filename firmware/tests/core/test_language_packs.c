#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "test_pack.h"

static uint8_t pack[TEST_PACK_CAPACITY];
static char english[STRING_COUNT][TEST_PACK_TEXT_CAPACITY];

static void remember_english(void)
{
    i18n_detach();
    for (uint32_t id = 0; id < STRING_COUNT; id++) {
        strncpy(english[id], i18n_text((StringId)id), TEST_PACK_TEXT_CAPACITY - 1);
    }
}

static uint32_t placeholder_mask(const char *text)
{
    uint32_t mask = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (cursor[0] == '{' && cursor[1] >= '0' && cursor[1] <= '9' && cursor[2] == '}') {
            mask |= 1u << (cursor[1] - '0');
        }
    }
    return mask;
}

static void the_english_pack_is_attached_and_fits_the_players_memory(void)
{
    uint32_t size = test_pack_load(ECHO_ENGLISH_PACK, pack, sizeof pack);
    CHECK(size > I18N_INDEX_BYTES);
    CHECK(test_pack_memory_need(pack) <= I18N_PACK_CAPACITY);
    CHECK(i18n_attach(pack, size));
    CHECK(strcmp("en", i18n_code()) == 0);
    i18n_detach();
}

static void the_english_pack_holds_the_built_in_english_text(void)
{
    uint32_t size = test_pack_load(ECHO_ENGLISH_PACK, pack, sizeof pack);
    CHECK(i18n_attach(pack, size));
    for (uint32_t id = 0; id < STRING_COUNT; id++) {
        CHECK(strcmp(english[id], test_pack_text(pack, (StringId)id)) == 0);
    }
    i18n_detach();
}

static void the_test_pack_builder_writes_the_same_bytes_as_the_tool(void)
{
    static uint8_t built[TEST_PACK_CAPACITY];
    uint32_t size = test_pack_load(ECHO_ENGLISH_PACK, pack, sizeof pack);
    uint32_t built_size = test_pack_build(built, (TestPackLanguage){"en", "English", NULL});
    CHECK_EQUAL(size, built_size);
    CHECK(memcmp(pack, built, size) == 0);
}

static void the_test_language_is_attached_and_fits_the_players_memory(void)
{
    uint32_t size = test_pack_load(ECHO_PSEUDO_PACK, pack, sizeof pack);
    CHECK(test_pack_memory_need(pack) <= I18N_PACK_CAPACITY);
    CHECK(i18n_attach(pack, size));
    CHECK(strcmp("qps", i18n_code()) == 0);
    CHECK(strcmp("[Pseudo]", test_pack_text(pack, STR_LANGUAGE_NAME)) == 0);
    i18n_detach();
}

static void the_test_language_changes_every_string_and_brackets_it(void)
{
    uint32_t size = test_pack_load(ECHO_PSEUDO_PACK, pack, sizeof pack);
    CHECK(i18n_attach(pack, size));
    for (uint32_t id = 0; id < STRING_COUNT; id++) {
        const char *text = test_pack_text(pack, (StringId)id);
        CHECK(strcmp(english[id], text) != 0);
        CHECK_EQUAL('[', text[0]);
        CHECK_EQUAL(']', text[strlen(text) - 1]);
        CHECK(strlen(text) > strlen(english[id]) || id == STR_LANGUAGE_NAME);
    }
    i18n_detach();
}

static void the_test_language_keeps_every_placeholder(void)
{
    uint32_t size = test_pack_load(ECHO_PSEUDO_PACK, pack, sizeof pack);
    CHECK(i18n_attach(pack, size));
    for (uint32_t id = 0; id < STRING_COUNT; id++) {
        CHECK_EQUAL(placeholder_mask(english[id]),
                    placeholder_mask(test_pack_text(pack, (StringId)id)));
    }
    i18n_detach();
}

int main(void)
{
    remember_english();
    static const TestCase cases[] = {
        TEST_CASE(the_english_pack_is_attached_and_fits_the_players_memory),
        TEST_CASE(the_english_pack_holds_the_built_in_english_text),
        TEST_CASE(the_test_pack_builder_writes_the_same_bytes_as_the_tool),
        TEST_CASE(the_test_language_is_attached_and_fits_the_players_memory),
        TEST_CASE(the_test_language_changes_every_string_and_brackets_it),
        TEST_CASE(the_test_language_keeps_every_placeholder),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
