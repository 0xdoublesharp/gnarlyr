#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/app.h"
#include "core/key.h"
#include "settings/headphone_profile.h"
#include "ui/list_view.h"
#include "ui/theme.h"

enum {
    USB_DAC_MENU_TEXT_CAPACITY = 12,
    USB_DAC_MENU_NAME_CAPACITY = HEADPHONE_PROFILE_NAME_BYTES + 8
};

typedef enum UsbDacRow {
    USB_DAC_ROW_VOLUME,
    USB_DAC_ROW_GAIN,
    USB_DAC_ROW_OUTPUT,
    USB_DAC_ROW_HEADPHONES,
    USB_DAC_ROW_COMPUTER,
    USB_DAC_ROW_CHARGE,
    USB_DAC_ROW_COUNT
} UsbDacRow;

typedef enum UsbDacAction {
    USB_DAC_ACTION_NONE,
    USB_DAC_ACTION_REDRAW,
    USB_DAC_ACTION_CLOSE,
    USB_DAC_ACTION_VOLUME,
    USB_DAC_ACTION_GAIN,
    USB_DAC_ACTION_OUTPUT,
    USB_DAC_ACTION_HEADPHONES,
    USB_DAC_ACTION_COMPUTER,
    USB_DAC_ACTION_CHARGE
} UsbDacAction;

typedef struct UsbDacMenuConfig {
    const Theme *theme;
    AppDacSettings settings;
    const HeadphoneProfiles *profiles;
} UsbDacMenuConfig;

typedef struct UsbDacMenu {
    ListModel model;
    ListView view;
    AppDacSettings settings;
    const HeadphoneProfiles *profiles;
    bool adjusting;
    char volume[USB_DAC_MENU_TEXT_CAPACITY];
    char profile[USB_DAC_MENU_NAME_CAPACITY];
} UsbDacMenu;

void usb_dac_menu_open(UsbDacMenu *menu, UsbDacMenuConfig config, uint32_t now_ms);
void usb_dac_menu_show_volume(UsbDacMenu *menu, uint8_t volume);
UsbDacAction usb_dac_menu_key(UsbDacMenu *menu, Key key, uint32_t now_ms);
int usb_dac_menu_rows(const Theme *theme);
