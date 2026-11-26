#include "battery_model_format.h"

#include "hal/storage.h"

const char BATTERY_MODEL_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/battery_model.bin";

bool battery_model_save(BatteryEstimate *estimate)
{
    if (!estimate->dirty) {
        return true;
    }
    uint8_t bytes[BATTERY_MODEL_BYTES] = {0};
    record_begin(bytes, &BATTERY_MODEL);
    bytes[SHOW_TIME_BYTE] = estimate->show_time;
    bytes_put_le16(bytes + HABIT_FIELD, estimate->habit);
    for (unsigned i = 0; i < BATTERY_LOAD_COUNT; i++) {
        bytes_put_le16(bytes + RATE_FIELD + i * 2, estimate->rate[i]);
        bytes[LEARNED_FIELD + i] = estimate->learned[i];
    }
    record_seal(bytes, SEAL_FIELD);
    if (!hal_internal_file_write(BATTERY_MODEL_PATH, bytes, sizeof bytes)) {
        return false;
    }
    estimate->dirty = false;
    return true;
}
