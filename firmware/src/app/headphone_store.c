#include "headphone_store.h"

#include <stddef.h>

#include "hal/audio.h"
#include "hal/storage.h"

const char HEADPHONE_STORE_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/headphones.bin";

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    uint8_t prefix[HEADPHONE_FILE_BYTES];
    if (offset > sizeof prefix || length > sizeof prefix - offset ||
        !hal_internal_file_read(HEADPHONE_STORE_PATH, prefix, offset + length)) {
        return false;
    }
    uint8_t *destination = bytes;
    for (uint32_t index = 0; index < length; index++) {
        destination[index] = prefix[offset + index];
    }
    return true;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    return hal_internal_file_write_at(HEADPHONE_STORE_PATH, offset, bytes, length);
}

HeadphoneAudio headphone_store_parameters(const HeadphoneProfiles *profiles)
{
    if (profiles->current == HEADPHONE_PROFILE_NONE) {
        return headphone_audio_defaults();
    }
    const HeadphoneProfile *profile = &profiles->slots[profiles->current];
    uint32_t trim = headphone_profile_trim(profile->trim_half_db);
    uint32_t left = (uint32_t)(100 - (profile->balance > 0 ? profile->balance : 0));
    uint32_t right = (uint32_t)(100 + (profile->balance < 0 ? profile->balance : 0));
    return (HeadphoneAudio){(trim * left + 50) / 100, (trim * right + 50) / 100,
                            profile->volume_ceiling};
}

void headphone_store_load(HeadphoneStore *store)
{
    *store = (HeadphoneStore){.profiles = headphone_profiles_defaults(),
                              .active = headphone_audio_defaults()};
    StoreFile file = {
        .source = {read_file, NULL}, .write = write_file, .size = HEADPHONE_FILE_BYTES};
    store->load = headphone_file_open(&store->file, file, &store->profiles);
    store->active = headphone_store_parameters(&store->profiles);
    hal_audio_set_headphones(store->active);
}
