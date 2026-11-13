#include "screens/card_format_screen.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/clock.h"
#include "hal/power.h"
#include "hal/system.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/navigation.h"

enum {
    PHASE_NOTICE,
    PHASE_WARNING,
    PHASE_CONFIRM,
    PHASE_WORKING,
    PROGRESS_MS = 250,
    FORMAT_TIMEOUT_MS = 120000,
    SDHC_SECTORS = 32u * 2 * 1024 * 1024
};

static bool powered(void)
{
    HalBattery battery = hal_power_battery();
    return battery.usb_power || (battery.known && battery.percent > 30);
}

static StringId refusal(CardFormatScreen *screen)
{
    HalCardInfo card = hal_card_info();
    if (!card.present || card.sectors == 0) {
        return STR_CARD_FORMAT_INSERT;
    }
    if (card.generation != screen->card.generation || card.sectors != screen->card.sectors) {
        return STR_CARD_FORMAT_CHANGED;
    }
    if (card.sectors < 131072) {
        return STR_CARD_FORMAT_UNSUPPORTED;
    }
    if (card.usb_busy) {
        return STR_CARD_FORMAT_USB;
    }
    if (hal_file_volume() == HAL_VOLUME_CARD &&
        player_status(screen->config.player).state == PLAYER_PLAYING) {
        return STR_CARD_FORMAT_PLAYING;
    }
    return powered() ? STRING_COUNT : STR_CARD_FORMAT_BATTERY;
}

static void message(CardFormatScreen *screen, StringId id)
{
    StringBuilder text = string_builder_make(screen->message, sizeof screen->message);
    string_builder_append(&text, i18n_text(id));
}

static bool allowed(CardFormatScreen *screen)
{
    StringId problem = refusal(screen);
    if (problem == STRING_COUNT) {
        return true;
    }
    screen->phase = PHASE_NOTICE;
    screen->screen.title = i18n_text(STR_CARD_FORMAT);
    message(screen, problem);
    return false;
}

static bool progress(void *context, uint32_t done, uint32_t total)
{
    CardFormatScreen *screen = context;
    uint32_t now_ms = hal_clock_milliseconds();
    hal_system_keep_alive();
    if (!powered() || now_ms - screen->shown_ms > FORMAT_TIMEOUT_MS) {
        return false;
    }
    if (screen->config.notice != NULL &&
        (done == 0 || done == total || now_ms - screen->progress_ms >= PROGRESS_MS)) {
        screen->progress_ms = now_ms;
        char text[64];
        StringBuilder line = string_builder_make(text, sizeof text);
        i18n_format_counts(&line, STR_CARD_FORMAT_PROGRESS, done * 100 / total, 0);
        screen->config.notice(i18n_text(STR_CARD_FORMAT), text);
    }
    return true;
}

static ScreenAction run(CardFormatScreen *screen)
{
    if (!allowed(screen)) {
        return screen_action_none();
    }
    if (screen->config.prepare != NULL) {
        screen->config.prepare();
    }
    screen->phase = PHASE_WORKING;
    screen->shown_ms = hal_clock_milliseconds();
    screen->progress_ms = screen->shown_ms;
    bool success = hal_card_format(screen->card, progress, screen);
    screen->phase = PHASE_NOTICE;
    screen->screen.title = i18n_text(STR_CARD_FORMAT);
    StringId result = success ? STR_CARD_FORMAT_DONE : STR_CARD_FORMAT_FAILED;
    message(screen, result);
    if (screen->config.finished != NULL) {
        screen->config.finished(result);
        return screen_action_pop_to_root();
    }
    return screen_action_none();
}

static ScreenAction key(void *context, KeyEvent event, uint32_t now_ms)
{
    (void)now_ms;
    CardFormatScreen *screen = context;
    NavigationIntent intent = navigation_from_key(event).intent;
    if (intent == NAVIGATION_BACK || intent == NAVIGATION_HOME) {
        return screen_action_pop();
    }
    if (screen->phase == PHASE_CONFIRM &&
        (intent == NAVIGATION_PREVIOUS || intent == NAVIGATION_NEXT)) {
        screen->erase = !screen->erase;
    }
    if (intent != NAVIGATION_SELECT) {
        return screen_action_none();
    }
    if (screen->phase == PHASE_WARNING && allowed(screen)) {
        screen->phase = PHASE_CONFIRM;
        screen->screen.title = i18n_text(STR_CARD_FORMAT_CONFIRM);
        screen->erase = false;
        return screen_action_none();
    }
    if (screen->phase == PHASE_CONFIRM) {
        return screen->erase ? run(screen) : screen_action_pop();
    }
    return screen_action_pop();
}

static void draw_choice(CardFormatScreen *screen, Canvas *canvas, Rect area, bool erase)
{
    const Theme *theme = screen->config.theme;
    bool selected = screen->erase == erase;
    if (selected) {
        canvas_fill_rect(canvas, area, theme->colors.selection_background);
    }
    TextStyle style = {.font = theme->fonts.body,
                       .color = selected ? theme->colors.selection_text : theme->colors.text};
    area.x += theme->metrics.padding;
    area.width -= 2 * theme->metrics.padding;
    label_draw(canvas, style,
               (Label){area, i18n_text(erase ? STR_CARD_FORMAT_ERASE : STR_CARD_FORMAT_CANCEL)});
}

static void draw(void *context, Canvas *canvas)
{
    CardFormatScreen *screen = context;
    const Theme *theme = screen->config.theme;
    header_draw(canvas, theme,
                screen->phase == PHASE_CONFIRM ? i18n_text(STR_CARD_FORMAT_CONFIRM)
                                               : i18n_text(STR_CARD_FORMAT));
    Rect area = theme_content_area(theme);
    int row = theme->metrics.row_height;
    Rect text = {area.x + theme->metrics.padding, area.y + theme->metrics.padding,
                 area.width - 2 * theme->metrics.padding, area.height - 2 * row};
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    label_draw_wrapped(canvas, style, (Label){text, screen->message});
    Rect bottom = {area.x, area.y + area.height - 2 * row, area.width, row};
    if (screen->phase == PHASE_CONFIRM) {
        draw_choice(screen, canvas, bottom, false);
        bottom.y += row;
        draw_choice(screen, canvas, bottom, true);
    } else if (screen->phase == PHASE_WARNING) {
        bottom.x += theme->metrics.padding;
        bottom.width -= 2 * theme->metrics.padding;
        bottom.height = 2 * row;
        label_draw_wrapped(canvas, style, (Label){bottom, i18n_text(STR_CARD_FORMAT_NEXT)});
    }
}

Screen *card_format_screen_init(CardFormatScreen *screen, CardFormatConfig config)
{
    *screen = (CardFormatScreen){.config = config, .card = hal_card_info(), .phase = PHASE_WARNING};
    screen->screen = (Screen){.title = i18n_text(STR_CARD_FORMAT),
                              .context = screen,
                              .takes_every_key = true,
                              .stays_on_wake = true,
                              .handle_key = key,
                              .draw = draw};
    if (allowed(screen)) {
        char size[16];
        StringBuilder number = string_builder_make(size, sizeof size);
        uint32_t tenths = (uint32_t)(((uint64_t)screen->card.sectors * 512 + 50000000) / 100000000);
        string_builder_append_unsigned(&number, tenths / 10);
        string_builder_append_char(&number, '.');
        string_builder_append_unsigned(&number, tenths % 10);
        const char *arguments[] = {size, i18n_text(screen->card.sectors > SDHC_SECTORS
                                                       ? STR_CARD_FORMAT_EXFAT
                                                       : STR_CARD_FORMAT_FAT32)};
        StringBuilder text = string_builder_make(screen->message, sizeof screen->message);
        i18n_format(&text, STR_CARD_FORMAT_WARNING, arguments, 2);
    }
    return &screen->screen;
}
