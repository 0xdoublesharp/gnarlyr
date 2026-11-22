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
    SETTINGS_CHOICE_COUNT = 40,
    SETTINGS_MAIN_CHOICE_COUNT = 31,
    SETTINGS_SAFE_VOLUME_OPTIONS = 6,
    SETTINGS_STATUS_CHOICE = 20,
    SETTINGS_STATUS_COUNT = 6,
    SETTINGS_STANDBY_CHOICE = SETTINGS_MAIN_CHOICE_COUNT + 5,
    SETTINGS_POWER_CHOICES = 3,
    SETTINGS_NOW_PLAYING_CHOICE = SETTINGS_CHOICE_COUNT - 1,
    SETTINGS_LANGUAGE_CHOICE = 10,
    SETTINGS_TEXT_SIZE_CHOICE = 8,
    SETTINGS_USB_MODE_CHOICE = 18,
    SETTINGS_SOUND_FIRST = 0,
    SETTINGS_CUSTOM_EQ_ITEM = 3,
    SETTINGS_PARAMETRIC_ITEM = 4,
    SETTINGS_HEADPHONES_ITEM = 5,
    SETTINGS_PLAYBACK_FIRST = 10,
    SETTINGS_TRANSITION_LENGTH_ITEM = 14,
    SETTINGS_DISPLAY_FIRST = 20,
    SETTINGS_STATUS_BAR_ITEM = 26,
    SETTINGS_HOME_ITEM = 28,
    SETTINGS_DATE_TIME_ITEM = 31,
    SETTINGS_POWER_FIRST = 32,
    SETTINGS_POWER_ITEM = 33,
    SETTINGS_CONNECTIONS_FIRST = 35,
    SETTINGS_BLUETOOTH_ITEM = 37,
    SETTINGS_LIBRARY_FIRST = 38,
    SETTINGS_RESCAN_ITEM = 38,
    SETTINGS_HIDDEN_ITEM = 39,
    SETTINGS_EJECT_ITEM = 40,
    SETTINGS_MOUNT_ITEM = 41,
    SETTINGS_FORMAT_ITEM = 42,
    SETTINGS_RESTORE_ITEM = 43,
    SETTINGS_ITEM_COUNT = 44,
    SETTINGS_MESSAGE_CAPACITY = 64,
    SETTINGS_COUNTDOWN_CAPACITY = 16
};

typedef enum SettingsGroup {
    SETTINGS_GROUP_SOUND,
    SETTINGS_GROUP_PLAYBACK,
    SETTINGS_GROUP_DISPLAY,
    SETTINGS_GROUP_LANGUAGE,
    SETTINGS_GROUP_POWER,
    SETTINGS_GROUP_CONNECTIONS,
    SETTINGS_GROUP_BUTTONS,
    SETTINGS_GROUP_LIBRARY,
    SETTINGS_GROUP_ABOUT,
    SETTINGS_GROUP_COUNT
} SettingsGroup;

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
    Screen *card_format;
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

typedef struct SettingsChooseTables {
    const SettingChoice *choices;
    const ButtonChoices *buttons;
} SettingsChooseTables;

typedef struct SettingsEntry {
    const SettingChoice *choice;
    SettingsScreens *owner;
} SettingsEntry;

typedef struct SettingsGroupEntry {
    SettingsScreens *owner;
    SettingsGroup group;
} SettingsGroupEntry;

struct SettingsScreens {
    SettingsContext context;
    const Theme *theme;
    StringId choice_preview;
    SettingsPreview preview;
    const Settings *(*preview_settings)(SettingsScreens *screens, bool active);
    MenuScreen menu;
    MenuScreen group_menu;
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
    const char *safe_volume_options[SETTINGS_SAFE_VOLUME_OPTIONS];
    PlugChooser usb;
    bool card_needs_format;
    SettingsEntry entries[SETTINGS_CHOICE_COUNT];
    SettingsGroupEntry group_entries[SETTINGS_GROUP_COUNT];
    MenuItem groups[SETTINGS_GROUP_COUNT];
    MenuItem items[SETTINGS_ITEM_COUNT];
};

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context,
                              const Theme *theme);
#ifdef ECHO_DEBUG_LOOP
extern const SettingsChooseTables SETTINGS_CHOOSE_TABLES;
#endif
Screen *settings_screens_usb_modes(SettingsScreens *screens, bool prompt);

ScreenAction settings_screens_sleep(SettingsScreens *screens);
MenuItem hidden_choice_item(SettingsScreens *screens);
MenuItem settings_backup_item(SettingsScreens *screens);
