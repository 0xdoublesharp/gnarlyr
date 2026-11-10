#include "device/power_history.h"

void power_history_record(PowerHistory *history, PowerSample sample)
{
    history->samples[history->recorded % POWER_HISTORY_SAMPLES] = sample;
    history->recorded++;
}

uint32_t power_history_count(const PowerHistory *history)
{
    return history->recorded < POWER_HISTORY_SAMPLES ? history->recorded : POWER_HISTORY_SAMPLES;
}

PowerSample power_history_at(const PowerHistory *history, uint32_t index)
{
    uint32_t oldest = history->recorded - power_history_count(history);
    return history->samples[(oldest + index) % POWER_HISTORY_SAMPLES];
}
