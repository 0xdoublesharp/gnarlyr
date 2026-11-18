#include "check.h"
#include "device/key_ladder.h"
#include "device/key_ladder_model.h"

static void check_next(KeyLadderModel *model, Key key, bool pressed)
{
    HalKeyTransition transition = {0};
    CHECK(key_ladder_model_next(model, &transition));
    CHECK_EQUAL(key, transition.key);
    CHECK_EQUAL(pressed, transition.pressed);
}

static void check_empty(KeyLadderModel *model)
{
    HalKeyTransition transition = {0};
    CHECK(!key_ladder_model_next(model, &transition));
}

static void the_lowest_held_key_sets_the_reading(void)
{
    CHECK_EQUAL(KEY_LADDER_IDLE_READING, key_ladder_reading(0));
    CHECK_EQUAL(242, key_ladder_reading(1u << KEY_PLAY));
    CHECK_EQUAL(1, key_ladder_reading(1u << KEY_PLAY | 1u << KEY_VOLUME_DOWN));
    CHECK_EQUAL(122, key_ladder_reading(1u << KEY_PLAY | 1u << KEY_VOLUME_UP));
    CHECK_EQUAL(242, key_ladder_reading(1u << KEY_PLAY | 1u << KEY_MENU | 1u << KEY_POWER));
    for (int key = KEY_VOLUME_UP; key <= KEY_BACK; key++) {
        Key decoded = KEY_POWER;
        CHECK(key_ladder_decode(key_ladder_reading((uint8_t)(1u << key)), &decoded));
        CHECK_EQUAL(key, decoded);
    }
}

static void holding_play_and_adding_vol_down_reads_as_the_players_log(void)
{
    KeyLadderModel model;
    key_ladder_model_init(&model);
    key_ladder_model_set(&model, KEY_PLAY, true, 36500);
    CHECK_EQUAL(242, model.reading);
    check_next(&model, KEY_PLAY, true);
    key_ladder_model_set(&model, KEY_VOLUME_DOWN, true, 36583);
    CHECK_EQUAL(1, model.reading);
    check_next(&model, KEY_PLAY, false);
    check_next(&model, KEY_VOLUME_DOWN, true);
    check_empty(&model);
}

static void releasing_vol_then_play_returns_through_the_play_band(void)
{
    KeyLadderModel model;
    key_ladder_model_init(&model);
    key_ladder_model_set(&model, KEY_PLAY, true, 100);
    key_ladder_model_set(&model, KEY_VOLUME_UP, true, 200);
    key_ladder_model_set(&model, KEY_VOLUME_UP, false, 300);
    CHECK_EQUAL(242, model.reading);
    key_ladder_model_set(&model, KEY_PLAY, false, 400);
    check_next(&model, KEY_PLAY, true);
    check_next(&model, KEY_PLAY, false);
    check_next(&model, KEY_VOLUME_UP, true);
    check_next(&model, KEY_VOLUME_UP, false);
    check_next(&model, KEY_PLAY, true);
    check_next(&model, KEY_PLAY, false);
    check_empty(&model);
}

static void releasing_play_under_a_held_volume_key_is_invisible(void)
{
    KeyLadderModel model;
    key_ladder_model_init(&model);
    key_ladder_model_set(&model, KEY_PLAY, true, 100);
    key_ladder_model_set(&model, KEY_VOLUME_UP, true, 200);
    key_ladder_model_set(&model, KEY_PLAY, false, 300);
    CHECK_EQUAL(122, model.reading);
    check_next(&model, KEY_PLAY, true);
    check_next(&model, KEY_PLAY, false);
    check_next(&model, KEY_VOLUME_UP, true);
    check_empty(&model);
    key_ladder_model_set(&model, KEY_VOLUME_UP, false, 400);
    check_next(&model, KEY_VOLUME_UP, false);
    check_empty(&model);
}

static void pressing_play_under_a_held_volume_key_is_invisible(void)
{
    KeyLadderModel model;
    key_ladder_model_init(&model);
    key_ladder_model_set(&model, KEY_VOLUME_DOWN, true, 100);
    key_ladder_model_set(&model, KEY_PLAY, true, 200);
    CHECK_EQUAL(1, model.reading);
    check_next(&model, KEY_VOLUME_DOWN, true);
    check_empty(&model);
}

static void power_is_its_own_input(void)
{
    KeyLadderModel model;
    key_ladder_model_init(&model);
    key_ladder_model_set(&model, KEY_PLAY, true, 100);
    key_ladder_model_set(&model, KEY_POWER, true, 200);
    check_next(&model, KEY_PLAY, true);
    check_next(&model, KEY_POWER, true);
    check_empty(&model);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_lowest_held_key_sets_the_reading),
        TEST_CASE(holding_play_and_adding_vol_down_reads_as_the_players_log),
        TEST_CASE(releasing_vol_then_play_returns_through_the_play_band),
        TEST_CASE(releasing_play_under_a_held_volume_key_is_invisible),
        TEST_CASE(pressing_play_under_a_held_volume_key_is_invisible),
        TEST_CASE(power_is_its_own_input),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
