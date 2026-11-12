#include "settings_screens.h"

#include <stddef.h>

#include "core/i18n.h"
#include "hal/clock.h"

static bool counted;
static uint16_t records;

static HiddenTracks tracks_of(const SettingsScreens *screens)
{
    return (HiddenTracks){.store = screens->context.library, .player = screens->context.player};
}

static uint16_t record_count(void)
{
    if (!counted) {
        HiddenTracksHead head;
        records = hidden_tracks_read_head(&head);
        counted = true;
    }
    return records;
}

static bool shown(const void *context)
{
    (void)context;
    return record_count() != 0;
}

static int no_current(const void *context)
{
    (void)context;
    return -1;
}

static const char *show_detail(const void *context, int index)
{
    (void)context;
    (void)index;
    return i18n_text(STR_SHOW_TRACK);
}

static const StringId SHOWN_TEXT[] = {STR_TRACK_SHOWN, STR_SHOW_FAILED, STR_TRACK_SHOWN_LATER,
                                      STR_SHOW_FAILED, STR_SHOW_FAILED};

static void show_again(void *context, int index)
{
    SettingsScreens *screens = context;
    HiddenResult result = hidden_tracks_unhide(tracks_of(screens), (uint16_t)index);
    counted = false;
    toast_show(screens->context.toast, i18n_text(SHOWN_TEXT[result]), hal_clock_milliseconds());
}

static Screen *list_hidden(void *context, int index)
{
    SettingsScreens *screens = context;
    HiddenTracksChoice *hidden = &screens->hidden;
    (void)index;
    int count = 0;
    for (uint16_t record = 0; record < record_count(); record++) {
        MixIdentity identity;
        hidden->labels[count][0] = 0;
        if (hidden_tracks_read(record, &identity)) {
            hidden_tracks_file_name(identity.path, hidden->labels[count],
                                    HIDDEN_TRACKS_LABEL_BYTES);
        }
        hidden->options[count] = hidden->labels[count];
        count++;
    }
    ChoiceConfig config = {.title = i18n_text(STR_HIDDEN_TRACKS),
                           .options = hidden->options,
                           .option_count = count,
                           .current = no_current,
                           .choose = show_again,
                           .open = list_hidden,
                           .detail = show_detail,
                           .context = screens,
                           .theme = screens->theme};
    return count != 0 ? choice_screen_init(&hidden->choice, config) : NULL;
}

static ScreenAction open_hidden(void *context)
{
    SettingsScreens *screens = context;
    Screen *screen = list_hidden(screens, 0);
    if (screen == NULL) {
        toast_show(screens->context.toast, i18n_text(STR_NO_HIDDEN_TRACKS),
                   hal_clock_milliseconds());
        return screen_action_none();
    }
    return screen_action_push(screen);
}

MenuItem hidden_choice_item(SettingsScreens *screens)
{
    counted = false;
    return (MenuItem){.label = i18n_text(STR_HIDDEN_TRACKS),
                      .select = open_hidden,
                      .context = screens,
                      .opens = true,
                      .shown = shown,
                      .shown_context = screens};
}
