#include <stddef.h>
#include <stdint.h>

#include "app/app.h"
#include "app_internal.h"
#include "card_files.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "fuzz_source.h"
#include "music_check.h"
#include "player.h"

enum {
    ACTION_LIMIT = 128,
    TAP_MS = 30,
    LONG_MS = 1000
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void add_library_files(uint8_t control)
{
    static FixtureLibrary fixture;
    fixture_library_open(&fixture);
    uint32_t covers_size = 0;
    const uint8_t *covers = fixture_covers_bytes(&covers_size);
    if ((control & 2u) == 0) {
        uint32_t size = (control & 1u) != 0 ? fixture.size / 3 : fixture.size;
        fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/library.db", fixture.bytes, size);
    }
    if ((control & 8u) == 0) {
        uint32_t size = (control & 4u) != 0 ? covers_size / 3 : covers_size;
        fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/covers.bin", covers, size);
    }
    fake_hal_add_file("/" FIRMWARE_DATA_DIRECTORY "/build.tmp", NULL,
                      (control & 16u) != 0 ? 4096 : 16 * 1024 * 1024);
    if ((control & 32u) == 0) {
        for (size_t index = 0; index < card_files_count(); index++) {
            if (card_files_slow(index)) {
                fake_hal_add_file(card_files_path(index), NULL, 4096);
            }
        }
    }
    if ((control & 64u) == 0) {
        music_check_remember((MusicFingerprint){.complete = true});
    }
    fake_hal_fail_music_writes((control & 128u) != 0);
}

static void check_app(void)
{
    fuzz_require(app_screen_title() != NULL);
    fuzz_require(app_screen_depth() > 0);
    PlayerStatus status = app_player_status();
    fuzz_require(status.state >= PLAYER_IDLE && status.state <= PLAYER_PAUSED);
    fuzz_require(status.volume <= HAL_AUDIO_VOLUME_MAX);
}

static void apply_environment(uint8_t value)
{
    switch (value >> 5) {
    case 1:
        fake_hal_audio_finish_track();
        break;
    case 2:
        fake_hal_audio_fail_track();
        break;
    case 3:
        fake_hal_audio_set_jack((HalAudioJack)(value % (HAL_AUDIO_JACK_BALANCED + 1)));
        break;
    case 4:
        fake_hal_queue_headset_command((HalHeadsetCommand)(value % (HAL_HEADSET_VOLUME_DOWN + 1)));
        break;
    case 5:
        fake_hal_set_usb_cable((value & 1u) != 0);
        fake_hal_set_usb_session((value & 2u) != 0);
        break;
    case 6:
        fake_hal_set_battery((HalBattery){
            .percent = value % 101, .usb_power = (value & 1u) != 0, .charging = (value & 2u) != 0});
        break;
    default:
        break;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    app_reset_for_test();
    fake_hal_reset();
    add_library_files(size > 0 ? data[0] : 0);
    uint32_t now_ms = 1000;
    fake_hal_set_milliseconds(now_ms);
    app_start();
    size_t limit = size < ACTION_LIMIT ? size : ACTION_LIMIT;
    for (size_t index = 0; index < limit; index++) {
        uint8_t value = data[index];
        apply_environment(value);
        Key key = (Key)(value % KEY_COUNT);
        uint32_t duration = (value & 8u) != 0 ? LONG_MS : TAP_MS;
        fake_hal_queue_transition(key, true, now_ms);
        fake_hal_queue_transition(key, false, now_ms + duration);
        now_ms += duration + 20;
        if ((index & 3u) == 3u || index + 1 == limit) {
            fake_hal_set_milliseconds(now_ms);
            app_step();
            check_app();
        }
    }
    app_stop();
    return 0;
}
