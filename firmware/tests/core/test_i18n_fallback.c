#include <string.h>

#include "check.h"
#include "core/i18n.h"
#include "test_pack.h"

static uint8_t pack[TEST_PACK_CAPACITY];

static void without_a_pack_every_text_is_a_question_mark(void)
{
    i18n_detach();
    for (uint32_t id = 0; id < STRING_COUNT; id++) {
        CHECK(strcmp("?", i18n_text((StringId)id)) == 0);
    }
}

static void without_a_pack_a_formatted_text_is_the_mark_too(void)
{
    char text[16];
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_detach();
    i18n_format_counts(&builder, STR_CHAPTER_OF, 3, 12);
    CHECK(strcmp("?", text) == 0);
}

static void a_pack_replaces_the_mark(void)
{
    uint32_t size =
        test_pack_build(pack, (TestPackLanguage){"qps", "[Pseudo]", test_pack_bracketed});
    CHECK(i18n_attach(pack, size));
    CHECK(strcmp("[?]", i18n_text(STR_SETTINGS)) == 0);
    CHECK(strcmp("?", i18n_text(STR_LANGUAGE_NAME)) == 0);
    CHECK(strcmp("[Pseudo]", test_pack_text(pack, STR_LANGUAGE_NAME)) == 0);
    i18n_detach_segment();
    CHECK(strcmp("?", i18n_text(STR_LANGUAGE_NAME)) == 0);
    i18n_detach();
    CHECK(strcmp("?", i18n_text(STR_SETTINGS)) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(without_a_pack_every_text_is_a_question_mark),
        TEST_CASE(without_a_pack_a_formatted_text_is_the_mark_too),
        TEST_CASE(a_pack_replaces_the_mark),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
