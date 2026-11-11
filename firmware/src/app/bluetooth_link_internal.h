#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bluetooth_link.h"

static inline bool bluetooth_link_same_address(const HalBluetoothDevice *first,
                                               const HalBluetoothDevice *second)
{
    for (size_t index = 0; index < HAL_BLUETOOTH_ADDRESS_SIZE; index++) {
        if (first->address[index] != second->address[index]) {
            return false;
        }
    }
    return true;
}

static inline int bluetooth_link_find_known(const BluetoothLink *link,
                                            const HalBluetoothDevice *device)
{
    for (int index = 0; index < link->saved.count; index++) {
        if (bluetooth_link_same_address(&link->saved.devices[index], device)) {
            return index;
        }
    }
    return -1;
}

void bluetooth_link_reset_session(BluetoothLink *link, uint32_t now_ms);
void bluetooth_link_wake(BluetoothLink *link, uint32_t now_ms);
