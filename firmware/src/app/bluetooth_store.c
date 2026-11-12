#include "bluetooth_store.h"

#include <stddef.h>
#include <stdint.h>

#include "hal/storage.h"

const char BLUETOOTH_STORE_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/bluetooth.bin";

enum {
    HEADER_SIZE = 3,
    VERSION_AT = 2,
    DEVICE_SIZE = HAL_BLUETOOTH_ADDRESS_SIZE + HAL_BLUETOOTH_NAME_CAPACITY,
    VERSION = 1,
    ENABLED_AT = HEADER_SIZE,
    COUNT_AT = ENABLED_AT + 1,
    DEVICES_AT = COUNT_AT + 1,
    OUTPUT_AT = DEVICES_AT + BLUETOOTH_STORE_DEVICES * DEVICE_SIZE,
    RECORD_SIZE = OUTPUT_AT + 1
};

static bool has_address(const HalBluetoothDevice *device)
{
    for (size_t index = 0; index < HAL_BLUETOOTH_ADDRESS_SIZE; index++) {
        if (device->address[index] != 0) {
            return true;
        }
    }
    return false;
}

static void read_device(const uint8_t *bytes, HalBluetoothDevice *device)
{
    for (size_t index = 0; index < HAL_BLUETOOTH_ADDRESS_SIZE; index++) {
        device->address[index] = bytes[index];
    }
    for (size_t index = 0; index < HAL_BLUETOOTH_NAME_CAPACITY; index++) {
        device->name[index] = (char)bytes[HAL_BLUETOOTH_ADDRESS_SIZE + index];
    }
    device->name[HAL_BLUETOOTH_NAME_CAPACITY - 1] = '\0';
}

static void write_device(const HalBluetoothDevice *device, uint8_t *bytes)
{
    for (size_t index = 0; index < HAL_BLUETOOTH_ADDRESS_SIZE; index++) {
        bytes[index] = device->address[index];
    }
    for (size_t index = 0; index < HAL_BLUETOOTH_NAME_CAPACITY; index++) {
        bytes[HAL_BLUETOOTH_ADDRESS_SIZE + index] = (uint8_t)device->name[index];
    }
}

static uint32_t read_record(uint8_t *record)
{
    HalFile file;
    if (!hal_file_open(&file, BLUETOOTH_STORE_PATH)) {
        return 0;
    }
    uint32_t size = file.size < RECORD_SIZE ? file.size : RECORD_SIZE;
    bool read = size == RECORD_SIZE && hal_file_read(file, 0, record, size);
    hal_file_close(&file);
    if (!read || record[0] != 'B' || record[1] != 'T') {
        return 0;
    }
    return size;
}

static void load_list(const uint8_t *record, BluetoothSaved *saved)
{
    saved->enabled = record[ENABLED_AT] == 1;
    saved->on_bluetooth = record[OUTPUT_AT] == 1;
    for (uint8_t index = 0; index < record[COUNT_AT]; index++) {
        HalBluetoothDevice *device = &saved->devices[saved->count];
        read_device(record + DEVICES_AT + index * DEVICE_SIZE, device);
        if (has_address(device)) {
            saved->count++;
        }
    }
}

void bluetooth_store_load(BluetoothSaved *saved)
{
    static uint8_t record[RECORD_SIZE];
    *saved = (BluetoothSaved){.enabled = false, .count = 0};
    if (read_record(record) == RECORD_SIZE && record[VERSION_AT] == VERSION &&
        record[ENABLED_AT] <= 1 && record[OUTPUT_AT] <= 1 &&
        record[COUNT_AT] <= BLUETOOTH_STORE_DEVICES) {
        load_list(record, saved);
    }
}

void bluetooth_store_save(const BluetoothSaved *saved)
{
    static uint8_t record[RECORD_SIZE];
    for (size_t index = 0; index < RECORD_SIZE; index++) {
        record[index] = 0;
    }
    record[0] = 'B';
    record[1] = 'T';
    record[VERSION_AT] = VERSION;
    record[ENABLED_AT] = saved->enabled ? 1 : 0;
    record[COUNT_AT] =
        saved->count < BLUETOOTH_STORE_DEVICES ? saved->count : (uint8_t)BLUETOOTH_STORE_DEVICES;
    record[OUTPUT_AT] = saved->on_bluetooth ? 1 : 0;
    for (uint8_t index = 0; index < saved->count && index < BLUETOOTH_STORE_DEVICES; index++) {
        write_device(&saved->devices[index], record + DEVICES_AT + index * DEVICE_SIZE);
    }
    (void)hal_file_write(BLUETOOTH_STORE_PATH, record, RECORD_SIZE);
}
