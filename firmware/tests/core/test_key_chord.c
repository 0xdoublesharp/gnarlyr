#include "check.h"
#include "core/key_chord.h"

static KeyChord allowed_chord(void)
{
    return (KeyChord){.allowed = true};
}

static void a_play_release_and_volume_press_at_once_start_a_chord(void)
{
    KeyChord chord = allowed_chord();
    CHECK_EQUAL(KEY_CHORD_PASS, key_chord_take(&chord, KEY_PLAY, false, 36583));
    CHECK_EQUAL(KEY_CHORD_START, key_chord_take(&chord, KEY_VOLUME_DOWN, true, 36583));
    CHECK(chord.play_held);
}

static void a_volume_press_after_an_idle_reading_is_not_a_chord(void)
{
    KeyChord chord = allowed_chord();
    key_chord_take(&chord, KEY_PLAY, false, 200);
    CHECK_EQUAL(KEY_CHORD_PASS, key_chord_take(&chord, KEY_VOLUME_UP, true, 215));
    CHECK(!chord.play_held);
}

static void another_key_between_play_and_volume_is_not_a_chord(void)
{
    KeyChord chord = allowed_chord();
    key_chord_take(&chord, KEY_PLAY, false, 200);
    CHECK_EQUAL(KEY_CHORD_PASS, key_chord_take(&chord, KEY_MENU, true, 200));
    key_chord_take(&chord, KEY_MENU, false, 300);
    CHECK_EQUAL(KEY_CHORD_PASS, key_chord_take(&chord, KEY_VOLUME_UP, true, 300));
}

static void play_coming_back_as_the_volume_key_lets_go_keeps_the_chord(void)
{
    KeyChord chord = allowed_chord();
    key_chord_take(&chord, KEY_PLAY, false, 100);
    key_chord_take(&chord, KEY_VOLUME_UP, true, 100);
    CHECK_EQUAL(KEY_CHORD_PASS, key_chord_take(&chord, KEY_VOLUME_UP, false, 300));
    CHECK_EQUAL(KEY_CHORD_DROP, key_chord_take(&chord, KEY_PLAY, true, 300));
    CHECK(chord.play_held);
    key_chord_take(&chord, KEY_PLAY, false, 400);
    CHECK_EQUAL(KEY_CHORD_START, key_chord_take(&chord, KEY_VOLUME_UP, true, 400));
}

static void the_volume_key_letting_go_to_idle_ends_the_chord(void)
{
    KeyChord chord = allowed_chord();
    key_chord_take(&chord, KEY_PLAY, false, 100);
    key_chord_take(&chord, KEY_VOLUME_UP, true, 100);
    key_chord_take(&chord, KEY_VOLUME_UP, false, 300);
    CHECK_EQUAL(KEY_CHORD_PASS, key_chord_take(&chord, KEY_PLAY, true, 500));
    CHECK(!chord.play_held);
}

static void a_play_press_under_a_volume_key_held_first_still_counts(void)
{
    KeyChord chord = allowed_chord();
    key_chord_take(&chord, KEY_VOLUME_DOWN, true, 100);
    key_chord_take(&chord, KEY_VOLUME_DOWN, false, 300);
    CHECK_EQUAL(KEY_CHORD_PASS, key_chord_take(&chord, KEY_PLAY, true, 300));
}

static void outside_the_chord_screens_a_play_release_never_arms_a_chord(void)
{
    KeyChord chord = {.allowed = false};
    key_chord_take(&chord, KEY_PLAY, false, 100);
    CHECK_EQUAL(KEY_CHORD_PASS, key_chord_take(&chord, KEY_VOLUME_UP, true, 100));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_play_release_and_volume_press_at_once_start_a_chord),
        TEST_CASE(a_volume_press_after_an_idle_reading_is_not_a_chord),
        TEST_CASE(another_key_between_play_and_volume_is_not_a_chord),
        TEST_CASE(play_coming_back_as_the_volume_key_lets_go_keeps_the_chord),
        TEST_CASE(the_volume_key_letting_go_to_idle_ends_the_chord),
        TEST_CASE(a_play_press_under_a_volume_key_held_first_still_counts),
        TEST_CASE(outside_the_chord_screens_a_play_release_never_arms_a_chord),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
