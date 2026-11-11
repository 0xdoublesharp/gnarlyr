#pragma once

struct SettingsContext;

void settings_action_save(struct SettingsContext *context);
void settings_action_apply_and_save(struct SettingsContext *context);
int settings_action_current_shuffle(const void *context);
void settings_action_choose_shuffle(void *context, int index);
int settings_action_current_repeat(const void *context);
void settings_action_choose_repeat(void *context, int index);
int settings_action_current_sleep(const void *context);
void settings_action_choose_sleep(void *context, int index);
int settings_action_active_slot(const void *context);
int settings_action_current_equalizer(const void *context);
const char *settings_action_describe_equalizer(const void *context, int current);
void settings_action_choose_equalizer(void *context, int index);
void settings_action_apply_replay_gain(struct SettingsContext *context);
void settings_action_choose_replay_gain(void *context, int index);
void settings_action_apply_transitions(struct SettingsContext *context);
int settings_action_current_transition(const void *context);
void settings_action_choose_transition(void *context, int index);
