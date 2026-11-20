#include "headphone_store.h"

#include "hal/audio.h"

void headphone_store_load(HeadphoneStore *store)
{
    *store = (HeadphoneStore){.profiles = headphone_profiles_defaults(),
                              .active = headphone_audio_defaults()};
    store->load = headphone_file_open(&store->file, headphone_store_file(), &store->profiles);
    store->active = headphone_store_parameters(&store->profiles);
    if (store->file.needs_reload) {
        store->active.ceiling = 0;
    }
    hal_audio_set_headphones(store->active);
}
