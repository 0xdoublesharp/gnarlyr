#include "equalizer_ui.h"

#include <stddef.h>

#include "hal/audio.h"
#include "hal/clock.h"

static EqualizerPresets presets;

static int active_slot(const EqualizerUi *ui)
{
    const Settings *settings = &ui->store->settings;
    if (settings->equalizer == EQUALIZER_CUSTOM) {
        return equalizer_presets_matching_slot(ui->presets, settings->equalizer_gains);
    }
    if (settings->equalizer == EQUALIZER_PARAMETRIC) {
        for (int slot = 0; slot < EQUALIZER_SLOT_COUNT; slot++) {
            if ((parametric_store_bank()->used >> slot & 1) != 0 &&
                parametric_same(&settings->parametric, &parametric_store_bank()->slots[slot])) {
                return slot;
            }
        }
    }
    return -1;
}

static void message(EqualizerUi *ui, bool saved, int slot)
{
    StringBuilder builder = string_builder_make(ui->message, sizeof ui->message);
    if (saved) {
        i18n_format_counts(&builder, STR_EQ_SAVED, (uint32_t)slot + 1, 0);
    } else {
        string_builder_append(&builder, i18n_text(STR_EQ_SLOT_NOT_SAVED));
    }
    toast_show(ui->toast, ui->message, hal_clock_milliseconds());
}

static void save(EqualizerUi *ui)
{
    ui->store->equalizer_slot = (int8_t)active_slot(ui);
    settings_store_capture(ui->store, player_status(ui->player));
    settings_store_apply_equalizer(&ui->store->settings);
    if (!settings_store_save(ui->store)) {
        toast_show(ui->toast, i18n_text(STR_SETTINGS_NOT_SAVED), hal_clock_milliseconds());
    }
}

static bool commit(void *context, const ParametricEq *eq, int slot)
{
    EqualizerUi *ui = context;
    ParametricBank bank = *parametric_store_bank();
    bank.custom = *eq;
    if (slot >= 0) {
        bank.slots[slot] = *eq;
        bank.used |= (uint8_t)(1u << slot);
    }
    if (!parametric_store_save(&ui->store->parametric, &bank)) {
        message(ui, false, slot);
        return false;
    }
    ui->store->settings.parametric = *eq;
    ui->store->settings.equalizer = EQUALIZER_PARAMETRIC;
    save(ui);
    if (slot >= 0) {
        message(ui, true, slot);
    }
    return true;
}

static void preview(void *context, const SettingsEqualizerCurve *curve)
{
    (void)context;
    HalAudioEqualizer equalizer = {.band_count = curve->band_count,
                                   .pre_gain = curve->pre_gain,
                                   .is_parametric = curve->is_parametric,
                                   .parametric = curve->parametric};
    for (int band = 0; band < curve->band_count; band++) {
        equalizer.band_hz[band] = curve->band_hz[band];
        equalizer.gain_half_db[band] = curve->gain_half_db[band];
    }
    hal_audio_set_equalizer(&equalizer);
}

static bool slot_used(const void *context, int slot)
{
    const EqualizerUi *ui = context;
    return equalizer_presets_slot_used(ui->presets, slot);
}

static void save_graphic(void *context, int slot)
{
    EqualizerUi *ui = context;
    bool saved =
        equalizer_presets_save_slot(ui->presets, slot, ui->store->settings.equalizer_gains);
    ui->store->equalizer_slot = (int8_t)active_slot(ui);
    message(ui, saved, slot);
}

static void changed(void *context, bool finished)
{
    EqualizerUi *ui = context;
    if (finished) {
        save(ui);
    }
}

static int current(const void *context)
{
    const EqualizerUi *ui = context;
    int slot = active_slot(ui);
    return slot < 0 ? (int)ui->store->settings.equalizer
                    : equalizer_presets_slot_option(ui->presets, slot);
}

static void choose(void *context, int option)
{
    EqualizerUi *ui = context;
    Settings next = ui->store->settings;
    if (!equalizer_presets_choose(ui->presets, option, &next)) {
        toast_show(ui->toast, i18n_text(STR_EQ_FILE_UNREADABLE), hal_clock_milliseconds());
        return;
    }
    if (next.equalizer == EQUALIZER_PARAMETRIC && !commit(ui, &next.parametric, -1)) {
        return;
    }
    ui->store->settings = next;
    save(ui);
    if (ui->presets->trimmed) {
        toast_show(ui->toast, i18n_text(STR_PEQ_TRIMMED), hal_clock_milliseconds());
    }
}

static Screen *open(void *context, int option)
{
    EqualizerUi *ui = context;
    int slot = equalizer_presets_option_slot(ui->presets, option);
    if (ui->store->settings.equalizer == EQUALIZER_PARAMETRIC) {
        ui->parametric.slot = slot < 0 ? 0 : slot;
        return &ui->parametric.screen;
    }
    if ((slot >= 0 || option == EQUALIZER_CUSTOM) &&
        ui->store->settings.equalizer == EQUALIZER_CUSTOM) {
        ui->graphic.slot = slot < 0 ? 0 : slot;
        return &ui->graphic.screen;
    }
    return NULL;
}

static void enter_browser(void *context, uint32_t now_ms)
{
    EqualizerBrowser *browser = context;
    EqualizerUi *ui = (EqualizerUi *)((char *)browser - offsetof(EqualizerUi, browser));
    parametric_store_read(&ui->store->parametric, NULL);
    ui->presets->parametric = &ui->store->parametric;
    equalizer_presets_read_slots(ui->presets);
    equalizer_presets_scan(ui->presets);
    browser->choice.config.option_count = ui->presets->option_count;
    ui->enter_browser(context, now_ms);
}

static void enter_graphic(void *context, uint32_t now_ms)
{
    EqualizerScreen *editor = context;
    EqualizerUi *ui = editor->config.context;
    parametric_store_read(&ui->store->parametric, NULL);
    ui->presets->parametric = &ui->store->parametric;
    equalizer_presets_read_slots(ui->presets);
    ui->enter_graphic(context, now_ms);
}

static void enter_parametric(void *context, uint32_t now_ms)
{
    ParametricScreen *editor = context;
    EqualizerUi *ui = editor->config.context;
    parametric_store_read(&ui->store->parametric, NULL);
    ui->presets->parametric = &ui->store->parametric;
    equalizer_presets_read_slots(ui->presets);
    ui->enter_parametric(context, now_ms);
}

void equalizer_ui_init(EqualizerUi *ui, SettingsStore *store, Player *player, Toast *toast,
                       const Theme *theme)
{
    *ui = (EqualizerUi){
        .store = store, .player = player, .toast = toast, .theme = theme, .presets = &presets};
    ui->presets->parametric = &store->parametric;
    parametric_store_read(&store->parametric, NULL);
    equalizer_presets_scan(ui->presets);
    ChoiceConfig choice = {.title = i18n_text(STR_EQUALIZER),
                           .options = ui->presets->options,
                           .option_count = ui->presets->option_count,
                           .current = current,
                           .choose = choose,
                           .open = open,
                           .context = ui,
                           .theme = theme};
    EqualizerBrowserConfig browser = {
        .choice = choice, .settings = &store->settings, .presets = ui->presets, .preview = preview};
    Screen *screen = equalizer_browser_init(&ui->browser, browser);
    ui->enter_browser = screen->enter;
    screen->enter = enter_browser;
    EqualizerScreenConfig graphic = {.settings = &store->settings,
                                     .changed = changed,
                                     .preview = preview,
                                     .slot_used = slot_used,
                                     .save_slot = save_graphic,
                                     .context = ui,
                                     .theme = theme};
    screen = equalizer_screen_init(&ui->graphic, graphic);
    ui->enter_graphic = screen->enter;
    screen->enter = enter_graphic;
    ParametricScreenConfig parametric = {.settings = &store->settings,
                                         .commit = commit,
                                         .preview = preview,
                                         .context = ui,
                                         .theme = theme};
    screen = parametric_screen_init(&ui->parametric, parametric);
    ui->enter_parametric = screen->enter;
    screen->enter = enter_parametric;
    equalizer_ui_cancel(ui);
}

void equalizer_ui_cancel(EqualizerUi *ui)
{
    if (ui->browser.previewing || ui->graphic.previewing || ui->parametric.previewing) {
        ui->browser.previewing = false;
        ui->graphic.previewing = false;
        ui->parametric.previewing = false;
        settings_store_apply_equalizer(&ui->store->settings);
    }
}
