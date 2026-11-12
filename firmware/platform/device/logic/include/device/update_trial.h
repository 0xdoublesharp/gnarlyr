#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    UPDATE_TRIAL_BYTES = 8,
    UPDATE_TRIAL_FILE_BYTES = 512,
    UPDATE_TRIAL_LIMIT = 3,
    UPDATE_TRIAL_SETTLE_MS = 20000,
    UPDATE_TRIAL_RESET_HOLD_MS = 10000
};

#define UPDATE_TRIAL_PATH "/" FIRMWARE_DATA_DIRECTORY "/trial.bin"
#define UPDATE_BOTH_SLOTS_PATH "/BOTHSLOT.REQ"

typedef enum UpdateTrialState {
    UPDATE_TRIAL_NONE,
    UPDATE_TRIAL_RUNNING,
    UPDATE_TRIAL_PROMOTING,
    UPDATE_TRIAL_ROLLED_BACK
} UpdateTrialState;

typedef struct UpdateTrial {
    UpdateTrialState state;
    uint8_t attempts;
} UpdateTrial;

typedef enum UpdateTrialStep {
    UPDATE_TRIAL_STEP_NOTHING,
    UPDATE_TRIAL_STEP_CHARGED,
    UPDATE_TRIAL_STEP_ROLL_BACK,
    UPDATE_TRIAL_STEP_REPORT
} UpdateTrialStep;

UpdateTrial update_trial_decode(const uint8_t *bytes);
void update_trial_encode(UpdateTrial trial, uint8_t *bytes);
UpdateTrialStep update_trial_boot(UpdateTrial *trial);
bool update_trial_charge(UpdateTrial *trial);
bool update_trial_settle(UpdateTrial *trial);
bool update_trial_both_slots_requested(const uint8_t *bytes, uint32_t length);
