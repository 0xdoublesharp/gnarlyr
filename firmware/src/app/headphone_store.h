#pragma once

#include "path_file.h"
#include "player.h"
#include "settings/headphone_file.h"
#include "settings_store.h"

extern const char HEADPHONE_STORE_PATH[];

typedef struct HeadphoneStore {
    HeadphoneFile file;
    HeadphoneProfiles profiles;
    HeadphoneAudio active;
    StoreLoad load;
} HeadphoneStore;

StoreFile headphone_store_file(void);
bool headphone_store_internal_read(const char *path, uint8_t *scratch, uint32_t capacity,
                                   const PathSlice *slice);
HeadphoneAudio headphone_store_parameters(const HeadphoneProfiles *profiles);
void headphone_store_load(HeadphoneStore *store);
bool headphone_store_commit(HeadphoneStore *store, const HeadphoneProfiles *profiles,
                            Player *player, SettingsStore *settings);
void headphone_store_choose_for_dac(HeadphoneStore *store, uint8_t chosen, Player *player,
                                    SettingsStore *settings);
void headphone_store_fill_slots(HeadphoneProfiles *profiles, uint8_t crossfeed);
