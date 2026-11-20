#pragma once

#include <stdbool.h>

#include "core/i18n.h"

struct SettingsContext;
struct Settings;

extern const StringId SETTINGS_ACTION_SLEEP_LABELS[];

void settings_action_save(struct SettingsContext *context);
void settings_action_candidate(struct SettingsContext *context, struct Settings *next);
bool settings_action_publish(struct SettingsContext *context, const struct Settings *next);
void settings_action_apply_and_save(struct SettingsContext *context);
int settings_action_current_shuffle(const void *context);
void settings_action_choose_shuffle(void *context, int index);
int settings_action_current_repeat(const void *context);
void settings_action_choose_repeat(void *context, int index);
int settings_action_current_sleep(const void *context);
void settings_action_choose_sleep(void *context, int index);
void settings_action_apply_replay_gain(struct SettingsContext *context);
int settings_action_current_replay_gain(const void *context);
void settings_action_choose_replay_gain(void *context, int index);
void settings_action_apply_transitions(struct SettingsContext *context);
int settings_action_current_transition(const void *context);
void settings_action_choose_transition(void *context, int index);
int settings_action_current_speed(const void *context);
void settings_action_choose_speed(void *context, int index);
bool settings_action_speed_available(const void *context);
int settings_action_current_now_playing(const void *context);
void settings_action_choose_now_playing(void *context, int index);
