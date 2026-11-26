#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/audio.h"
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

static HeadphoneProfiles capped_profile(void)
{
    HeadphoneProfiles candidate = named_profiles();
    candidate.current = 0;
    candidate.slots[0].volume_ceiling = 18;
    return candidate;
}

static void check_nothing_changed(uint8_t volume, uint8_t dac)
{
    CHECK_EQUAL(volume, player.volume);
    CHECK_EQUAL(volume, fake_hal_audio_volume());
    CHECK_EQUAL(dac, settings.settings.dac_volume);
    CHECK_EQUAL(volume, settings.settings.volume);
    SettingsStore stored;
    settings_store_load(&stored);
    CHECK_EQUAL(volume, stored.settings.volume);
    CHECK_EQUAL(dac, stored.settings.dac_volume);
    HeadphoneStore profiles;
    headphone_store_load(&profiles);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, profiles.profiles.current);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, store.profiles.current);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, store.active.left);
}

static void start_loud(void)
{
    reset();
    player_change_volume(&player, 60);
    settings_capture(&settings.settings, player_status(&player));
    CHECK(settings_store_save(&settings));
}

static void a_failed_profile_write_changes_no_volume_and_no_stored_setting(void)
{
    start_loud();
    uint8_t volume = player.volume;
    uint8_t dac = settings.settings.dac_volume;
    HeadphoneProfiles candidate = capped_profile();
    fake_hal_fail_internal_writes(HEADPHONE_STORE_PATH);
    CHECK(!headphone_store_commit(&store, &candidate, &player, &settings));
    check_nothing_changed(volume, dac);
}

static void a_failed_settings_write_changes_no_volume_and_no_profile(void)
{
    start_loud();
    uint8_t volume = player.volume;
    uint8_t dac = settings.settings.dac_volume;
    HeadphoneProfiles candidate = capped_profile();
    fake_hal_fail_internal_writes(SETTINGS_STORE_PATH);
    CHECK(!headphone_store_commit(&store, &candidate, &player, &settings));
    check_nothing_changed(volume, dac);
}

static void missing_profiles_keep_neutral_sound_without_editor_state(void)
{
    reset();
    CHECK_EQUAL(0, store.profiles.count);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, store.profiles.current);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, store.active.left);
    CHECK_EQUAL(60, store.active.ceiling);
}

static void a_commit_saves_through_its_own_file_callbacks_after_the_loader_is_gone(void)
{
    reset();
    store.file.file.source.read = NULL;
    store.file.file.write = NULL;
    HeadphoneProfiles candidate = named_profiles();
    candidate.current = 2;
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    HeadphoneStore restored;
    headphone_store_load(&restored);
    CHECK_EQUAL(STORE_READY, restored.load);
    CHECK_EQUAL(2, restored.profiles.current);
}

static void a_missing_profile_file_blocks_saves_until_it_is_made(void)
{
    fake_hal_reset();
    settings_store_load(&settings);
    CHECK(fixture_library_open(&fixture));
    player_init(&player, &fixture.library);
    headphone_store_load(&store);
    CHECK_EQUAL(STORE_UNREADABLE, store.load);
    CHECK(store.file.needs_reload);
    CHECK_EQUAL(0, store.active.ceiling);
    CHECK_EQUAL(0, hal_audio_headphones().ceiling);
}

static void an_unreadable_profile_file_applies_no_volume_until_a_load_succeeds(void)
{
    reset();
    HeadphoneProfiles candidate = capped_profile();
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    CHECK_EQUAL(18, hal_audio_headphones().ceiling);
    fake_hal_fail_internal_reads(HEADPHONE_STORE_PATH);
    headphone_store_load(&store);
    CHECK(store.file.needs_reload);
    CHECK_EQUAL(0, store.active.ceiling);
    CHECK_EQUAL(0, hal_audio_headphones().ceiling);
    CHECK(!headphone_store_commit(&store, &candidate, &player, &settings));
    CHECK_EQUAL(0, hal_audio_headphones().ceiling);
    fake_hal_fail_internal_reads(NULL);
    headphone_store_load(&store);
    CHECK_EQUAL(STORE_READY, store.load);
    CHECK_EQUAL(18, hal_audio_headphones().ceiling);
}

static void corrupt_both_banks(void)
{
    uint8_t garbage[HEADPHONE_FILE_BYTES / 2];
    memset(garbage, 0x5A, sizeof garbage);
    CHECK(hal_internal_file_write_at(HEADPHONE_STORE_PATH, 0, garbage, sizeof garbage));
    CHECK(
        hal_internal_file_write_at(HEADPHONE_STORE_PATH, sizeof garbage, garbage, sizeof garbage));
}

static void a_corrupt_profile_file_applies_no_volume_until_a_profile_is_saved(void)
{
    reset();
    HeadphoneProfiles candidate = capped_profile();
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    corrupt_both_banks();
    headphone_store_load(&store);
    CHECK_EQUAL(STORE_CORRUPT, store.load);
    CHECK_EQUAL(0, store.active.ceiling);
    CHECK_EQUAL(0, hal_audio_headphones().ceiling);
    player_change_volume(&player, 60);
    CHECK_EQUAL(0, fake_hal_audio_volume());
    CHECK(headphone_store_commit(&store, &candidate, &player, &settings));
    CHECK_EQUAL(18, hal_audio_headphones().ceiling);
    CHECK_EQUAL(0, player.volume);
    HeadphoneStore restored;
    headphone_store_load(&restored);
    CHECK_EQUAL(STORE_READY, restored.load);
    CHECK_EQUAL(18, restored.active.ceiling);
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
    CHECK_EQUAL(1, bytes[4]);
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

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(missing_profiles_keep_neutral_sound_without_editor_state),
        TEST_CASE(a_commit_saves_through_its_own_file_callbacks_after_the_loader_is_gone),
        TEST_CASE(a_missing_profile_file_blocks_saves_until_it_is_made),
        TEST_CASE(an_unreadable_profile_file_applies_no_volume_until_a_load_succeeds),
        TEST_CASE(a_corrupt_profile_file_applies_no_volume_until_a_profile_is_saved),
        TEST_CASE(a_profile_caps_music_usb_and_restored_levels_before_hardware_volume),
        TEST_CASE(switching_off_attenuation_lowers_both_saved_mode_volumes),
        TEST_CASE(rejected_edits_preserve_audio_and_the_current_settings_version),
        TEST_CASE(spoken_word_alone_gets_skip_silence_and_speech_boost),
        TEST_CASE(the_active_profile_crossfeed_overrides_the_default),
        TEST_CASE(a_failed_profile_write_changes_no_volume_and_no_stored_setting),
        TEST_CASE(a_failed_settings_write_changes_no_volume_and_no_profile),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
