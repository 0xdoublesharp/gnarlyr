#include "usb_dac_menu.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/audio.h"

enum {
    OUTPUT_CHOICES = 3,
    GAIN_CHOICES = 2
};

static const StringId LABELS[USB_DAC_ROW_COUNT] = {
    STR_VOLUME, STR_GAIN, STR_OUTPUT, STR_HEADPHONES, STR_FILE_TRANSFER, STR_CHARGE_ONLY};
static const StringId GAINS[GAIN_CHOICES] = {STR_GAIN_LOW, STR_GAIN_HIGH};
static const StringId OUTPUTS[OUTPUT_CHOICES] = {STR_OUTPUT_AUTO, STR_JACK_35, STR_JACK_44};

static int row_count(const void *context)
{
    (void)context;
    return USB_DAC_ROW_COUNT;
}

static const char *row_label(const void *context, int index)
{
    (void)context;
    return i18n_text(LABELS[index]);
}

static const char *row_detail(const void *context, int index)
{
    const UsbDacMenu *menu = context;
    switch (index) {
    case USB_DAC_ROW_VOLUME:
        return menu->volume;
    case USB_DAC_ROW_GAIN:
        return i18n_text(GAINS[menu->settings.gain % GAIN_CHOICES]);
    case USB_DAC_ROW_OUTPUT:
        return i18n_text(OUTPUTS[menu->settings.output % OUTPUT_CHOICES]);
    case USB_DAC_ROW_HEADPHONES:
        return menu->profile;
    default:
        break;
    }
    return NULL;
}

static void write_volume(UsbDacMenu *menu)
{
    StringBuilder builder = string_builder_make(menu->volume, sizeof menu->volume);
    string_builder_append_unsigned(&builder, menu->settings.volume);
}

static void write_profile(UsbDacMenu *menu)
{
    StringBuilder builder = string_builder_make(menu->profile, sizeof menu->profile);
    uint8_t current = menu->settings.headphones;
    if (menu->profiles == NULL || current >= menu->profiles->count) {
        string_builder_append(&builder, i18n_text(STR_OFF));
        return;
    }
    const char *name = menu->profiles->slots[current].name;
    if (name[0] != '\0') {
        string_builder_append(&builder, name);
        return;
    }
    char number[USB_DAC_MENU_TEXT_CAPACITY];
    StringBuilder digits = string_builder_make(number, sizeof number);
    string_builder_append_unsigned(&digits, (uint32_t)current + 1);
    const char *arguments[] = {number};
    i18n_format(&builder, STR_HEADPHONE_SLOT, arguments, 1);
}

void usb_dac_menu_show_volume(UsbDacMenu *menu, uint8_t volume)
{
    menu->settings.volume = volume;
    write_volume(menu);
}

int usb_dac_menu_rows(const Theme *theme)
{
    int rows = (theme->metrics.screen_height - theme->metrics.header_height -
                3 * theme->metrics.padding - font_line_height(theme->fonts.small)) /
               theme->metrics.row_height;
    return rows < USB_DAC_ROW_COUNT ? rows : USB_DAC_ROW_COUNT;
}

void usb_dac_menu_open(UsbDacMenu *menu, UsbDacMenuConfig config, uint32_t now_ms)
{
    *menu = (UsbDacMenu){.settings = config.settings, .profiles = config.profiles};
    menu->model =
        (ListModel){.context = menu, .count = row_count, .label = row_label, .detail = row_detail};
    write_volume(menu);
    write_profile(menu);
    list_view_init(&menu->view, &menu->model, config.theme, now_ms);
    list_view_fit_rows(&menu->view, usb_dac_menu_rows(config.theme));
}

static UsbDacAction next_profile(UsbDacMenu *menu)
{
    uint8_t count = menu->profiles != NULL ? menu->profiles->count : 0;
    uint8_t current = menu->settings.headphones;
    uint8_t next = current >= count ? 0 : (uint8_t)(current + 1);
    menu->settings.headphones = next >= count ? (uint8_t)HEADPHONE_PROFILE_NONE : next;
    write_profile(menu);
    return menu->settings.headphones == current ? USB_DAC_ACTION_NONE : USB_DAC_ACTION_HEADPHONES;
}

static UsbDacAction step_volume(UsbDacMenu *menu, int step)
{
    int level = menu->settings.volume + step;
    if (level < 0 || level > HAL_AUDIO_VOLUME_MAX) {
        return USB_DAC_ACTION_NONE;
    }
    menu->settings.volume = (uint8_t)level;
    write_volume(menu);
    return USB_DAC_ACTION_VOLUME;
}

static UsbDacAction adjust(UsbDacMenu *menu, Key key)
{
    if (key == KEY_VOLUME_UP || key == KEY_VOLUME_DOWN) {
        return step_volume(menu, key == KEY_VOLUME_UP ? 1 : -1);
    }
    if (key == KEY_PLAY || key == KEY_BACK || key == KEY_MENU) {
        menu->adjusting = false;
        return USB_DAC_ACTION_REDRAW;
    }
    return USB_DAC_ACTION_NONE;
}

static UsbDacAction choose(UsbDacMenu *menu)
{
    switch (menu->view.selected) {
    case USB_DAC_ROW_VOLUME:
        menu->adjusting = true;
        return USB_DAC_ACTION_REDRAW;
    case USB_DAC_ROW_GAIN:
        menu->settings.gain = (uint8_t)((menu->settings.gain + 1) % GAIN_CHOICES);
        return USB_DAC_ACTION_GAIN;
    case USB_DAC_ROW_OUTPUT:
        menu->settings.output = (uint8_t)((menu->settings.output + 1) % OUTPUT_CHOICES);
        return USB_DAC_ACTION_OUTPUT;
    case USB_DAC_ROW_HEADPHONES:
        return next_profile(menu);
    case USB_DAC_ROW_FILE_TRANSFER:
        return USB_DAC_ACTION_FILE_TRANSFER;
    default:
        break;
    }
    return USB_DAC_ACTION_CHARGE_ONLY;
}

UsbDacAction usb_dac_menu_key(UsbDacMenu *menu, Key key, uint32_t now_ms)
{
    if (menu->adjusting) {
        return adjust(menu, key);
    }
    switch (key) {
    case KEY_VOLUME_UP:
    case KEY_VOLUME_DOWN:
        list_view_move(&menu->view, key == KEY_VOLUME_UP ? 1 : -1, true, now_ms);
        return USB_DAC_ACTION_REDRAW;
    case KEY_PLAY:
        return choose(menu);
    case KEY_BACK:
    case KEY_MENU:
        return USB_DAC_ACTION_CLOSE;
    default:
        break;
    }
    return USB_DAC_ACTION_NONE;
}
