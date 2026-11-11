#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/bluetooth.h"

enum {
    BLUETOOTH_STORE_DEVICES = 6
};

extern const char BLUETOOTH_STORE_PATH[];

typedef struct BluetoothSaved {
    bool enabled;
    bool on_bluetooth;
    uint8_t count;
    HalBluetoothDevice devices[BLUETOOTH_STORE_DEVICES];
} BluetoothSaved;

void bluetooth_store_load(BluetoothSaved *saved);
void bluetooth_store_save(const BluetoothSaved *saved);
