#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/app.h"
#include "core/key.h"
#include "ui/list_view.h"
#include "ui/theme.h"

enum {
    USB_DAC_MENU_TEXT_CAPACITY = 12
};

typedef enum UsbDacRow {
    USB_DAC_ROW_VOLUME,
    USB_DAC_ROW_GAIN,
    USB_DAC_ROW_OUTPUT,
    USB_DAC_ROW_FILE_TRANSFER,
    USB_DAC_ROW_CHARGE_ONLY,
    USB_DAC_ROW_COUNT
} UsbDacRow;

typedef enum UsbDacAction {
    USB_DAC_ACTION_NONE,
    USB_DAC_ACTION_REDRAW,
    USB_DAC_ACTION_CLOSE,
    USB_DAC_ACTION_VOLUME,
    USB_DAC_ACTION_GAIN,
    USB_DAC_ACTION_OUTPUT,
    USB_DAC_ACTION_FILE_TRANSFER,
    USB_DAC_ACTION_CHARGE_ONLY
} UsbDacAction;

typedef struct UsbDacMenu {
    ListModel model;
    ListView view;
    AppDacSettings settings;
    bool adjusting;
    char volume[USB_DAC_MENU_TEXT_CAPACITY];
} UsbDacMenu;

void usb_dac_menu_open(UsbDacMenu *menu, const Theme *theme, AppDacSettings settings,
                       uint32_t now_ms);
UsbDacAction usb_dac_menu_key(UsbDacMenu *menu, Key key, uint32_t now_ms);
int usb_dac_menu_rows(const Theme *theme);
