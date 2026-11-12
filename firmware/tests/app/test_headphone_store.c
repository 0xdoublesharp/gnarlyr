#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "headphone_store.h"
#include "listening_effects.h"

static HeadphoneStore store;
static SettingsStore settings;
static Player player;
static FixtureLibrary fixture;

static void reset(void)
{
    fake_hal_reset();
    CHECK_EQUAL(HAL_RESERVE_CREATED,
                hal_internal_file_reserve(HEADPHONE_STORE_PATH, HEADPHONE_FILE_BYTES));
    settings_store_load(&settings);
    CHECK(fixture_library_open(&fixture));
    player_init(&player, &fixture.library);
    headphone_store_load(&store);
}

static HeadphoneProfiles named_profiles(void)
{
    HeadphoneProfiles profiles = headphone_profiles_defaults();
    profiles.count = 4;
    for (int index = 0; index < 4; index++) {
        profiles.slots[index] = headphone_profile_defaults();
        memcpy(profiles.slots[index].name, "Example", sizeof "Example");
    }
    return profiles;
}

static void missing_profiles_keep_neutral_sound_without_editor_state(void)
{
    reset();
    CHECK_EQUAL(0, store.profiles.count);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, store.profiles.current);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, store.active.left);
    CHECK_EQUAL(60, store.active.ceiling);
}

static void a_profile_caps_music_usb_and_restored_levels_before_hardware_volume(void)
{
    reset();
    player_change_volume(&player, 60);
    HeadphoneProfiles candidate = named_profiles();
    candidate.current = 0;
    candidate.slots[0].volume_ceiling = 18;
    candidate.slots[0].trim_half_db = 12;
    candidate.slots[0].balance = 25;
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    CHECK_EQUAL(18, player.volume);
    CHECK_EQUAL(18, fake_hal_audio_volume());
    CHECK_EQUAL(18, settings.settings.dac_volume);
    CHECK(store.active.left < store.active.right);
    CHECK(store.active.right < REPLAY_GAIN_UNITY);
    player_change_volume(&player, 100);
    CHECK_EQUAL(18, player.volume);
    CHECK_EQUAL(18, fake_hal_audio_volume());
    hal_audio_set_volume(60);
    CHECK_EQUAL(18, fake_hal_audio_volume());
    HeadphoneStore restored;
    headphone_store_load(&restored);
    CHECK_EQUAL(0, restored.profiles.current);
    CHECK_EQUAL(store.active.left, restored.active.left);
    fake_hal_replace_music_volume();
    headphone_store_load(&restored);
    CHECK_EQUAL(0, restored.profiles.current);
    CHECK_EQUAL(18, restored.active.ceiling);
}

static void switching_off_attenuation_lowers_both_saved_mode_volumes(void)
{
    reset();
    HeadphoneProfiles candidate = named_profiles();
    candidate.current = 0;
    candidate.slots[0].trim_half_db = 12;
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    player_change_volume(&player, 40 - player.volume);
    settings.settings.dac_volume = 50;
    candidate.current = HEADPHONE_PROFILE_NONE;
    uint8_t music = headphone_audio_switch_volume(store.active, headphone_audio_defaults(), 40);
    uint8_t usb = headphone_audio_switch_volume(store.active, headphone_audio_defaults(), 50);
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    CHECK_EQUAL(music, player.volume);
    CHECK_EQUAL(usb, settings.settings.dac_volume);
    CHECK_EQUAL(music, fake_hal_audio_volume());
    CHECK(music < 40 && usb < 50);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, store.active.left);
}

static void rejected_edits_preserve_audio_and_the_current_settings_version(void)
{
    reset();
    HeadphoneProfiles candidate = named_profiles();
    candidate.slots[0].trim_half_db = 25;
    int writes = fake_hal_write_count();
    uint8_t volume = player.volume;
    CHECK(!headphone_store_commit(&store, &candidate, &player, &settings));
    CHECK_EQUAL(writes, fake_hal_write_count());
    CHECK_EQUAL(volume, player.volume);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, store.profiles.current);
    CHECK(settings_store_save(&settings));
    uint32_t size;
    const uint8_t *bytes = fake_hal_internal_file_bytes(SETTINGS_STORE_PATH, &size);
    CHECK_EQUAL(SETTINGS_FILE_SIZE, size);
    CHECK_EQUAL(16, bytes[4]);
    CHECK_EQUAL(0, bytes[41]);
    Settings decoded;
    CHECK(settings_decode(bytes, SETTINGS_FILE_SIZE, &decoded));
}

static void spoken_word_alone_gets_skip_silence_and_speech_boost(void)
{
    reset();
    Settings chosen = settings.settings;
    chosen.skip_silence = true;
    chosen.speech_boost = true;
    HalAudioEffects music = listening_effects_choose(&chosen, &store.profiles, false);
    CHECK(!music.skip_silence);
    CHECK(!music.speech_boost);
    HalAudioEffects spoken = listening_effects_choose(&chosen, &store.profiles, true);
    CHECK(spoken.skip_silence);
    CHECK(spoken.speech_boost);
    chosen.speech_boost = false;
    CHECK(!listening_effects_choose(&chosen, &store.profiles, true).speech_boost);
}

static void the_active_profile_crossfeed_overrides_the_default(void)
{
    reset();
    Settings chosen = settings.settings;
    chosen.crossfeed = 2;
    CHECK_EQUAL(2, listening_effects_choose(&chosen, &store.profiles, false).crossfeed);
    HeadphoneProfiles candidate = named_profiles();
    candidate.current = 1;
    candidate.slots[1].crossfeed = 0;
    candidate.slots[0].crossfeed = 3;
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    CHECK_EQUAL(0, listening_effects_choose(&chosen, &store.profiles, true).crossfeed);
    candidate.current = 0;
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    CHECK_EQUAL(3, listening_effects_choose(&chosen, &store.profiles, true).crossfeed);
}

static void quick_setting_hooks_change_and_save_the_effects(void)
{
    reset();
    CHECK(listening_effects_toggle_skip_silence(&settings));
    CHECK(listening_effects_toggle_speech_boost(&settings));
    ListeningEffectsContext context = {&settings, &store, &player};
    CHECK(listening_effects_cycle_crossfeed(context));
    SettingsStore reloaded;
    settings_store_load(&reloaded);
    CHECK(reloaded.settings.skip_silence);
    CHECK(reloaded.settings.speech_boost);
    CHECK_EQUAL(1, reloaded.settings.crossfeed);
    for (int step = 0; step < 3; step++) {
        CHECK(listening_effects_cycle_crossfeed(context));
    }
    CHECK_EQUAL(0, settings.settings.crossfeed);
    HeadphoneProfiles candidate = named_profiles();
    candidate.current = 2;
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    CHECK(listening_effects_cycle_crossfeed(context));
    CHECK_EQUAL(1, store.profiles.slots[2].crossfeed);
    CHECK_EQUAL(0, settings.settings.crossfeed);
    HeadphoneStore restored;
    headphone_store_load(&restored);
    CHECK_EQUAL(1, restored.profiles.slots[2].crossfeed);
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(missing_profiles_keep_neutral_sound_without_editor_state),
        TEST_CASE(a_profile_caps_music_usb_and_restored_levels_before_hardware_volume),
        TEST_CASE(switching_off_attenuation_lowers_both_saved_mode_volumes),
        TEST_CASE(rejected_edits_preserve_audio_and_the_current_settings_version),
        TEST_CASE(spoken_word_alone_gets_skip_silence_and_speech_boost),
        TEST_CASE(the_active_profile_crossfeed_overrides_the_default),
        TEST_CASE(quick_setting_hooks_change_and_save_the_effects),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
