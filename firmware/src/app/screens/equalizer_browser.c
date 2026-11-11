#include "equalizer_browser.h"

#include <stddef.h>

#include "equalizer_plot.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/navigation.h"

static EqualizerResponse response_buffer;

void equalizer_browser_leave(EqualizerBrowser *browser)
{
    if (browser->previewing && browser->config.preview != NULL) {
        browser->config.preview(browser->config.choice.context, &browser->committed);
    }
    browser->previewing = false;
}

static bool selected_curve(EqualizerBrowser *browser, SettingsEqualizerCurve *curve)
{
    Settings selected = *browser->config.settings;
    int option = browser->choice.list.selected;
    if (!equalizer_presets_choose(browser->config.presets, option, &selected)) {
        return false;
    }
    *curve = settings_equalizer_curve(&selected);
    return equalizer_response_make(browser->response, curve);
}

static void refresh(EqualizerBrowser *browser)
{
    if (browser->preview_index == browser->choice.list.selected) {
        return;
    }
    browser->preview_index = browser->choice.list.selected;
    SettingsEqualizerCurve curve;
    browser->valid = selected_curve(browser, &curve);
    if (browser->config.preview != NULL) {
        browser->config.preview(browser->config.choice.context,
                                browser->valid ? &curve : &browser->committed);
        browser->previewing = true;
    }
}

static void enter(void *context, uint32_t now_ms)
{
    EqualizerBrowser *browser = context;
    browser->committed = settings_equalizer_curve(browser->config.settings);
    browser->preview_index = -1;
    browser->previewing = false;
    browser->choice_enter(context, now_ms);
    refresh(browser);
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    EqualizerBrowser *browser = context;
    NavigationIntent intent = navigation_from_key(event).intent;
    if (intent == NAVIGATION_SELECT && !browser->valid) {
        return screen_action_none();
    }
    if (intent == NAVIGATION_SELECT || intent == NAVIGATION_BACK || intent == NAVIGATION_HOME) {
        equalizer_browser_leave(browser);
    }
    ScreenAction action = browser->choose_key(context, event, now_ms);
    if (action.type != SCREEN_ACTION_NONE) {
        return action;
    }
    refresh(browser);
    return action;
}

static void update(void *context, uint32_t now_ms)
{
    EqualizerBrowser *browser = context;
    browser->choice.now_ms = now_ms;
    refresh(browser);
}

static void draw_options(Canvas *canvas, const EqualizerBrowser *browser, Rect area)
{
    const ChoiceScreen *choice = &browser->choice;
    const Theme *theme = choice->config.theme;
    int height = theme->metrics.row_height;
    int pad = theme->metrics.padding / 2;
    int count = choice->model.count(choice);
    int current = choice->config.current(choice->config.context);
    for (int row = 0; row < choice->list.visible_rows; row++) {
        int index = choice->list.top + row;
        if (index >= count) {
            break;
        }
        Rect bounds = {area.x, area.y + row * height, area.width, height};
        bool selected = index == choice->list.selected;
        if (selected) {
            canvas_fill_rect(canvas, bounds, theme->colors.header_background);
            canvas_fill_rect(
                canvas, (Rect){bounds.x, bounds.y, theme->metrics.scrollbar_inset, bounds.height},
                theme->colors.accent);
        }
        if (index == current) {
            int edge = theme->metrics.scrollbar_width;
            canvas_fill_rect(
                canvas,
                (Rect){bounds.x + bounds.width - edge, bounds.y + (height - edge) / 2, edge, edge},
                theme->colors.success);
        }
        Rect text = {bounds.x + pad, bounds.y, bounds.width - 2 * pad, height};
        label_draw(canvas,
                   (TextStyle){theme->fonts.body,
                               selected ? theme->colors.text : theme->colors.text_secondary},
                   (Label){text, choice->model.label(choice, index)});
    }
}

static void draw(void *context, Canvas *canvas)
{
    const EqualizerBrowser *browser = context;
    const Theme *theme = browser->config.choice.theme;
    const char *hint = i18n_text(STR_EQ_PRESET_HINT);
    EqualizerPlotLayout layout = equalizer_plot_layout(theme, hint, true);
    canvas_fill_rect(canvas,
                     (Rect){0, 0, theme->metrics.screen_width, theme->metrics.screen_height},
                     theme->colors.background);
    header_draw(canvas, theme, browser->choice.screen.title);
    draw_options(canvas, browser, layout.list);
    const char *name = browser->choice.model.label(&browser->choice, browser->choice.list.selected);
    const Font *font = text_width(theme->fonts.title, name) <= layout.primary.width
                           ? theme->fonts.title
                           : theme->fonts.small;
    label_draw(canvas, (TextStyle){font, theme->colors.text}, (Label){layout.primary, name});
    if (browser->valid) {
        equalizer_plot_draw(
            canvas, theme, layout.plot,
            (EqualizerPlotView){.response = browser->response, .selected_band = -1});
        equalizer_plot_caption(canvas, theme, layout.preamp, browser->response);
        label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_tertiary},
                   (Label){layout.reference, i18n_text(STR_EQ_REFERENCE)});
    } else {
        label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.danger},
                           (Label){layout.plot, i18n_text(STR_EQ_FILE_UNREADABLE)});
    }
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                       (Label){layout.hint, hint});
}

Screen *equalizer_browser_init(EqualizerBrowser *browser, EqualizerBrowserConfig config)
{
    browser->config = config;
    browser->response = &response_buffer;
    Screen *screen = choice_screen_init(&browser->choice, config.choice);
    browser->choose_key = screen->handle_key;
    browser->choice_enter = screen->enter;
    screen->handle_key = handle_key;
    screen->enter = enter;
    screen->update = update;
    screen->draw = draw;
    screen->paints_every_pixel = true;
    enter(browser, 0);
    EqualizerPlotLayout layout =
        equalizer_plot_layout(config.choice.theme, i18n_text(STR_EQ_PRESET_HINT), true);
    list_view_fit_rows(&browser->choice.list,
                       layout.list.height / config.choice.theme->metrics.row_height);
    return screen;
}
