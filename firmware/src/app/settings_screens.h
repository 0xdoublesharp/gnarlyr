#pragma once

#include <stdint.h>

#include "button_settings.h"
#include "core/debug_script.h"
#include "core/power_policy.h"
#include "equalizer_presets.h"
#include "headphone_editor.h"
#include "hidden_tracks.h"
#include "language.h"
#include "player.h"
#include "screens/bluetooth_screen.h"
#include "screens/card_format_screen.h"
#include "screens/equalizer_browser.h"
#include "screens/equalizer_screen.h"
#include "screens/parametric_screen.h"
#include "screens/plug_chooser.h"
#include "screens/set_clock_screen.h"
#include "settings/user_state.h"
#include "settings_preview.h"
#include "settings_store.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    SETTINGS_CHOICE_COUNT = 41,
    SETTINGS_MAIN_CHOICE_COUNT = 32,
    SETTINGS_STATUS_CHOICE = 21,
    SETTINGS_STATUS_COUNT = 6,
    SETTINGS_STANDBY_CHOICE = SETTINGS_MAIN_CHOICE_COUNT + 5,
    SETTINGS_POWER_CHOICES = 3,
    SETTINGS_NOW_PLAYING_CHOICE = SETTINGS_CHOICE_COUNT - 1,
    SETTINGS_LANGUAGE_CHOICE = 10,
    SETTINGS_USB_MODE_CHOICE = 19,
    SETTINGS_ABOUT_ITEM = 0,
    SETTINGS_BLUETOOTH_ITEM = 3,
    SETTINGS_DATE_TIME_ITEM = 8,
    SETTINGS_LANGUAGE_ITEM = 13,
    SETTINGS_CUSTOM_EQ_ITEM = 19,
    SETTINGS_RESCAN_ITEM = 23,
    SETTINGS_HIDDEN_ITEM = 24,
    SETTINGS_TRANSITION_LENGTH_ITEM = 32,
    SETTINGS_HEADPHONES_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 6,
    SETTINGS_FORMAT_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 9,
    SETTINGS_HOME_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 10,
    SETTINGS_PARAMETRIC_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 11,
    SETTINGS_POWER_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 12,
    SETTINGS_NOW_PLAYING_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 13,
    SETTINGS_RESTORE_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 14,
    SETTINGS_CONTROLS_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 15,
    SETTINGS_EJECT_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 7,
    SETTINGS_MOUNT_ITEM = SETTINGS_MAIN_CHOICE_COUNT + 8,
    SETTINGS_ITEM_COUNT = SETTINGS_MAIN_CHOICE_COUNT + 16,
    SETTINGS_MESSAGE_CAPACITY = 64,
    SETTINGS_COUNTDOWN_CAPACITY = 16
};

typedef struct SettingsContext {
    SettingsStore *store;
    HeadphoneStore *headphones;
    Player *player;
    PowerPolicy *power;
    Toast *toast;
    void (*format_prepare)(void);
    void (*reload)(StringId toast);
    bool *rescan_requested;
    bool *language_requested;
    LibraryStore *library;
    BluetoothScreen *bluetooth;
    Theme *theme;
    Screen *equalizer;
    Screen *graphic;
    Screen *parametric;
    EqualizerPresets *presets;
    const LanguageList *languages;
    char message[SETTINGS_MESSAGE_CAPACITY];
} SettingsContext;

typedef struct SettingsScreens SettingsScreens;

typedef struct SettingChoice {
    StringId title;
    const StringId *options;
    int option_count;
    int (*current)(const void *context);
    void (*choose)(void *context, int index);
    const char *(*describe)(const void *context, int current);
    void (*extend)(SettingsScreens *screens, ChoiceConfig *config);
} SettingChoice;

typedef struct SettingsEntry {
    const SettingChoice *choice;
    SettingsScreens *owner;
} SettingsEntry;

struct SettingsScreens {
    SettingsContext context;
    const Theme *theme;
    StringId choice_preview;
    SettingsPreview preview;
    const Settings *(*preview_settings)(SettingsScreens *screens, bool active);
    MenuScreen menu;
    union {
        MenuScreen status_menu;
        MenuScreen power_menu;
        MenuScreen backup_menu;
    };
    union {
        MenuItem status_items[SETTINGS_STATUS_COUNT];
        MenuItem power_items[SETTINGS_POWER_CHOICES];
        MenuItem backup_items[USER_STATE_BACKUPS];
    };
    UserStateList backups;
    union {
        ChoiceScreen choice;
        SetClockScreen clock;
        HiddenTracksChoice hidden;
        PlugChooser plug;
    };
    ButtonSettings buttons;
    HeadphoneEditor headphones;
    LanguageList languages;
    PlugChooser usb;
    CardFormatScreen card_format;
    bool card_needs_format;
    SettingsEntry entries[SETTINGS_CHOICE_COUNT];
    MenuItem items[SETTINGS_ITEM_COUNT];
};

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context,
                              const Theme *theme);
bool settings_screens_choose(SettingsScreens *screens, DebugSpan title, DebugSpan option);
Screen *settings_screens_usb_modes(SettingsScreens *screens, bool prompt);

ScreenAction settings_screens_sleep(SettingsScreens *screens);
MenuItem hidden_choice_item(SettingsScreens *screens);
MenuItem settings_backup_item(SettingsScreens *screens);
