#include "plug_modes.h"

static const uint8_t SWITCH_ORDER[] = {PLUG_MODE_COMPUTER, PLUG_MODE_CHARGE, PLUG_MODE_USB_POWER,
                                       PLUG_MODE_DAC};
static const StringId NAMES[PLUG_MODE_COUNT] = {
    STR_PLUG_ASK, STR_PLUG_COMPUTER, STR_PLUG_CHARGE_PLAY, STR_PLUG_RUN_USB, STR_PLUG_DAC};
static const StringId SHORT_NAMES[PLUG_MODE_COUNT] = {STR_PLUG_ASK, STR_PLUG_COMPUTER_SHORT,
                                                      STR_PLUG_CHARGE_PLAY_SHORT,
                                                      STR_PLUG_RUN_USB_SHORT, STR_PLUG_DAC_SHORT};
static const StringId LINES[PLUG_MODE_COUNT] = {STR_PLUG_ASK_LINE, STR_PLUG_COMPUTER_LINE,
                                                STR_PLUG_CHARGE_PLAY_LINE, STR_PLUG_RUN_USB_LINE,
                                                STR_PLUG_DAC_LINE};
static const uint8_t ICONS[PLUG_MODE_COUNT] = {ICON_USB, ICON_COMPUTER, ICON_BOLT, ICON_PLUG,
                                               ICON_SPEAKER};
static const uint8_t SAVED_BY_ANSWER[USB_CONNECT_COUNT] = {PLUG_MODE_ASK, PLUG_MODE_COMPUTER,
                                                           PLUG_MODE_CHARGE, PLUG_MODE_DAC};
static const uint8_t ANSWER_BY_MODE[PLUG_MODE_COUNT] = {
    USB_CONNECT_ASK, USB_CONNECT_STORAGE, USB_CONNECT_CHARGE, USB_CONNECT_CHARGE, USB_CONNECT_DAC};

static void add(PlugChoices *choices, PlugMode mode)
{
    choices->modes[choices->count++] = (uint8_t)mode;
}

static uint8_t row_of(const PlugChoices *choices, PlugMode mode)
{
    for (uint8_t row = 0; row < choices->count; row++) {
        if (choices->modes[row] == mode) {
            return row;
        }
    }
    return PLUG_NOT_MARKED;
}

static PlugMode in_use(PlugState state)
{
    return state.care == PLUG_CARE_USB ? PLUG_MODE_USB_POWER : PLUG_MODE_CHARGE;
}

PlugChoices plug_modes_choices(PlugState state)
{
    PlugChoices choices = {0};
    bool settings = state.place == PLUG_PLACE_SETTINGS;
    if (settings) {
        add(&choices, PLUG_MODE_ASK);
    }
    for (int index = 0; index < (int)sizeof SWITCH_ORDER; index++) {
        PlugMode mode = (PlugMode)SWITCH_ORDER[index];
        if (mode != PLUG_MODE_USB_POWER || settings || state.battery.usb_power) {
            add(&choices, mode);
        }
    }
    PlugMode current = settings ? state.saved : in_use(state);
    choices.marked =
        settings || state.battery.usb_power ? row_of(&choices, current) : PLUG_NOT_MARKED;
    bool prompt = state.place == PLUG_PLACE_PROMPT;
    choices.cursor = prompt || choices.marked == PLUG_NOT_MARKED ? 0 : choices.marked;
    return choices;
}

bool plug_modes_battery_ready(HalBattery battery)
{
    return battery.known && battery.millivolts >= PLUG_SAFE_MILLIVOLTS;
}

StringId plug_mode_name(PlugMode mode)
{
    return NAMES[mode];
}

StringId plug_mode_short_name(PlugMode mode)
{
    return SHORT_NAMES[mode];
}

StringId plug_mode_line(PlugMode mode, HalBattery battery)
{
    if (mode == PLUG_MODE_USB_POWER && battery.usb_power && !plug_modes_battery_ready(battery)) {
        return STR_PLUG_LOW_LINE;
    }
    return LINES[mode];
}

IconId plug_mode_icon(PlugMode mode)
{
    return (IconId)ICONS[mode];
}

PlugMode plug_mode_saved(const Settings *settings)
{
    if (settings->battery_care == PLUG_CARE_USB) {
        return PLUG_MODE_USB_POWER;
    }
    return (PlugMode)SAVED_BY_ANSWER[settings->usb_connect];
}

void plug_mode_save(Settings *settings, PlugMode mode)
{
    settings->usb_connect = (UsbConnect)ANSWER_BY_MODE[mode];
    settings->battery_care = mode == PLUG_MODE_USB_POWER ? PLUG_CARE_USB : PLUG_CARE_FULL;
}
