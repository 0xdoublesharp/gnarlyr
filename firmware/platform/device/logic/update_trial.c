#include "device/update_trial.h"

enum {
    STATE_AT = 4,
    ATTEMPTS_AT = 5,
    CHECK_AT = 6,
    VERSION_AT = 7,
    VERSION = 1,
    CHECK_SEED = 0x5A,
    MOST_ATTEMPTS = 0xFF
};

static const uint8_t MAGIC[STATE_AT] = {'D', 'S', 'U', 'T'};

static bool starts_with(const uint8_t *bytes, const uint8_t *text, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (bytes[index] != text[index]) {
            return false;
        }
    }
    return true;
}

static uint8_t check_byte(uint8_t state, uint8_t attempts)
{
    return (uint8_t)(state ^ attempts ^ CHECK_SEED);
}

UpdateTrial update_trial_decode(const uint8_t *bytes)
{
    uint8_t state = bytes[STATE_AT];
    uint8_t attempts = bytes[ATTEMPTS_AT];
    bool valid = starts_with(bytes, MAGIC, sizeof MAGIC) && bytes[VERSION_AT] == VERSION &&
                 state <= UPDATE_TRIAL_ROLLED_BACK &&
                 bytes[CHECK_AT] == check_byte(state, attempts);
    return valid ? (UpdateTrial){(UpdateTrialState)state, attempts}
                 : (UpdateTrial){UPDATE_TRIAL_NONE, 0};
}

void update_trial_encode(UpdateTrial trial, uint8_t *bytes)
{
    for (uint32_t index = 0; index < sizeof MAGIC; index++) {
        bytes[index] = MAGIC[index];
    }
    bytes[STATE_AT] = (uint8_t)trial.state;
    bytes[ATTEMPTS_AT] = trial.attempts;
    bytes[CHECK_AT] = check_byte((uint8_t)trial.state, trial.attempts);
    bytes[VERSION_AT] = VERSION;
}

bool update_trial_charge(UpdateTrial *trial)
{
    if (trial->state != UPDATE_TRIAL_RUNNING || trial->attempts == MOST_ATTEMPTS) {
        return false;
    }
    trial->attempts++;
    return true;
}

UpdateTrialStep update_trial_boot(UpdateTrial *trial)
{
    if (trial->state == UPDATE_TRIAL_ROLLED_BACK) {
        *trial = (UpdateTrial){UPDATE_TRIAL_NONE, 0};
        return UPDATE_TRIAL_STEP_REPORT;
    }
    if (trial->state == UPDATE_TRIAL_RUNNING && trial->attempts >= UPDATE_TRIAL_LIMIT) {
        return UPDATE_TRIAL_STEP_ROLL_BACK;
    }
    return update_trial_charge(trial) ? UPDATE_TRIAL_STEP_CHARGED : UPDATE_TRIAL_STEP_NOTHING;
}

bool update_trial_settle(UpdateTrial *trial)
{
    if (trial->state != UPDATE_TRIAL_RUNNING || trial->attempts == 0) {
        return false;
    }
    trial->attempts--;
    return true;
}
