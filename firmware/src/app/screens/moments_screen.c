#include "moments_screen.h"

#include <stddef.h>

#include "core/format.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "core/utf8.h"
#include "hal/storage.h"
#include "listening_work.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/message_screen.h"
#include "ui/navigation.h"

enum {
    MODE_LIST,
    MODE_CONFIRM,
    MODE_SAVE,
    MODE_DELETE,
    MODE_RESOLVE
};

static const char PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/moments.bin";
static MomentsWork *const work = &listening_module_work.as.moments;

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    HalFile file;
    if (!hal_file_open(&file, PATH)) {
        return false;
    }
    bool read = hal_file_read(file, offset, bytes, length);
    hal_file_close(&file);
    hal_audio_service();
    return read;
}

static bool write_file(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    bool written = hal_file_write_at(PATH, offset, bytes, length);
    hal_audio_service();
    return written;
}

static StoreFile file_io(uint32_t size)
{
    return (StoreFile){.source = {read_file, NULL}, .write = write_file, .size = size};
}

static void reload(void)
{
    HalFile file;
    if (!hal_file_open(&file, PATH)) {
        work->moments = (Moments){0};
        work->load = STORE_EMPTY;
        return;
    }
    uint32_t size = file.size;
    hal_file_close(&file);
    work->load = moments_open(&work->moments, file_io(size));
}

static bool writable(void)
{
    if (work->load != STORE_READY && work->load != STORE_EMPTY) {
        return false;
    }
    if (work->moments.file.size == 0) {
        HalReserveResult result = hal_file_reserve(PATH, MOMENTS_FILE_BYTES);
        if (result == HAL_RESERVE_CREATED) {
            uint8_t zeros[STORE_BLOCK_BYTES] = {0};
            uint8_t found[STORE_BLOCK_BYTES];
            (void)write_file(NULL, MOMENTS_BANK_BYTES, zeros, sizeof zeros);
            if (!read_file(NULL, MOMENTS_BANK_BYTES, found, sizeof found)) {
                return false;
            }
            for (unsigned index = 0; index < sizeof found; index++) {
                if (found[index] != 0) {
                    return false;
                }
            }
        } else if (result != HAL_RESERVE_EXISTS) {
            return false;
        }
        reload();
    }
    return !work->moments.needs_reload && work->moments.file.size >= MOMENTS_FILE_BYTES;
}

static void message(MomentsScreen *screen, StringId id)
{
    toast_show(screen->config.toast, i18n_text(id), screen->now_ms);
}

static bool working(const MomentsScreen *screen)
{
    return screen->mode >= MODE_SAVE;
}

static int row_count(const void *context)
{
    const MomentsScreen *screen = context;
    return screen->mode == MODE_CONFIRM ? 2 : work->moments.count + 1;
}

static const char *row_label(const void *context, int index)
{
    const MomentsScreen *screen = context;
    if (screen->mode == MODE_CONFIRM) {
        return i18n_text(index == 0 ? STR_MOMENT_KEEP : STR_DELETE);
    }
    if (index == 0) {
        return i18n_text(STR_SAVE_MOMENT);
    }
    int cached = index - work->first;
    return cached >= 0 && cached < THEME_MAX_ROWS ? work->labels[cached] : "";
}

static void cache_rows(MomentsScreen *screen)
{
    work->first = screen->list.top;
    for (int row = 0; row < THEME_MAX_ROWS; row++) {
        int index = work->first + row;
        Moment moment;
        StringBuilder builder = string_builder_make(work->labels[row], MOMENT_LABEL_BYTES);
        if (index > 0 && moments_read(&work->moments, (uint16_t)(index - 1), &moment)) {
            string_builder_append(&builder, moment.label);
        }
    }
}

static void fit_rows(MomentsScreen *screen)
{
    const Theme *theme = screen->config.theme;
    int hint = 2 * font_line_height(theme->fonts.small) + theme->metrics.padding;
    int height = theme_content_area(theme).height - hint;
    list_view_fit_rows(&screen->list, height / theme->metrics.row_height);
}

static void show_list(MomentsScreen *screen)
{
    screen->mode = MODE_LIST;
    screen->screen.title = i18n_text(STR_MOMENTS);
    list_view_init(&screen->list, &screen->model, screen->config.theme, screen->now_ms);
    fit_rows(screen);
    list_view_select(&screen->list, screen->selected, screen->now_ms);
    cache_rows(screen);
    wire_motion_end(screen->config.motion, screen);
}

static bool capture(MomentsScreen *screen)
{
    Player *player = screen->config.player;
    PlayerStatus status = player_status(player);
    if (!status.has_track) {
        message(screen, STR_MOMENT_NO_TRACK);
        return false;
    }
    LibraryTrack track;
    char title[MOMENT_LABEL_BYTES];
    char time[16];
    Moment *moment = &work->moment;
    *moment = (Moment){.position_ms = status.position_ms};
    if (mix_identity_read(player->library, status.track, &moment->identity) != STORE_READY ||
        !library_read_track(player->library, status.track, &track) ||
        !library_read_string(player->library, track.title, title, sizeof title)) {
        message(screen, STR_MOMENT_FAILED);
        return false;
    }
    format_duration(status.position_ms, time, sizeof time);
    StringBuilder builder = string_builder_make(moment->label, sizeof moment->label);
    size_t length = 0;
    while (length < sizeof title - 17 && title[length] != 0) {
        length++;
    }
    title[utf8_complete_length(title, length)] = 0;
    const char *arguments[] = {title, time};
    i18n_format(&builder, STR_MOMENT_LABEL, arguments, 2);
    return true;
}

static void save(MomentsScreen *screen)
{
    if (!capture(screen)) {
        return;
    }
    if (work->moments.count == MOMENTS_CAPACITY) {
        message(screen, STR_MOMENTS_FULL);
    } else if (writable() &&
               moments_add(&work->job, &work->moments, work->moments.generation, &work->moment)) {
        screen->mode = MODE_SAVE;
    } else {
        message(screen, STR_MOMENT_FAILED);
    }
}

static void select_row(MomentsScreen *screen)
{
    if (screen->mode == MODE_CONFIRM) {
        if (screen->list.selected == 0) {
            show_list(screen);
        } else if (moments_delete(&work->job, &work->moments, work->moments.generation,
                                  (uint16_t)(screen->selected - 1))) {
            screen->mode = MODE_DELETE;
        } else {
            message(screen, STR_MOMENT_FAILED);
            show_list(screen);
        }
        return;
    }
    screen->selected = (uint16_t)screen->list.selected;
    if (screen->selected == 0) {
        save(screen);
    } else if (moments_read(&work->moments, (uint16_t)(screen->selected - 1), &work->moment)) {
        moment_resolve_begin(&work->resolve, screen->config.player->library,
                             &work->moment.identity);
        screen->mode = MODE_RESOLVE;
    } else {
        message(screen, STR_MOMENTS_UNAVAILABLE);
    }
}

static void cancel(MomentsScreen *screen)
{
    if (screen->mode == MODE_SAVE || screen->mode == MODE_DELETE) {
        moments_abort(&work->job);
        reload();
    }
    wire_motion_end(screen->config.motion, screen);
    screen->mode = MODE_LIST;
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    MomentsScreen *screen = context;
    screen->now_ms = now_ms;
    Navigation navigation = navigation_from_key(event);
    if (navigation.intent == NAVIGATION_BACK || navigation.intent == NAVIGATION_HOME) {
        bool nested = screen->mode != MODE_LIST;
        cancel(screen);
        if (navigation.intent == NAVIGATION_HOME) {
            return screen_action_pop_to_root();
        }
        if (!nested) {
            return screen_action_pop();
        }
        show_list(screen);
    } else if (!working(screen)) {
        if (navigation.intent == NAVIGATION_NEXT || navigation.intent == NAVIGATION_PREVIOUS) {
            list_view_move(&screen->list, navigation.intent == NAVIGATION_NEXT ? 1 : -1, true,
                           now_ms);
            cache_rows(screen);
        } else if (navigation.intent == NAVIGATION_SELECT) {
            select_row(screen);
        } else if (navigation.intent == NAVIGATION_OPTIONS && screen->mode == MODE_LIST &&
                   screen->list.selected > 0) {
            screen->selected = (uint16_t)screen->list.selected;
            screen->mode = MODE_CONFIRM;
            list_view_init(&screen->list, &screen->model, screen->config.theme, now_ms);
            fit_rows(screen);
        }
    }
    return screen_action_none();
}

static bool play_moment(MomentsScreen *screen)
{
    Player *player = screen->config.player;
    LibraryTrack track;
    if (!library_read_track(player->library, work->resolve.track, &track) ||
        work->moment.position_ms > track.duration_ms) {
        return false;
    }
    BrowseQuery source = browse_root_query(BROWSE_SINGLE_TRACK);
    source.subject = work->resolve.track;
    if (!player_play_from(player, (PlayerListStart){.source = source,
                                                    .in_order = true,
                                                    .position_ms = work->moment.position_ms})) {
        return false;
    }
    player_seek(player, work->moment.position_ms);
    if (!player_save(player)) {
        return false;
    }
    screen->destination = screen->config.now_playing;
    return true;
}

static void update(void *context, uint32_t now_ms)
{
    MomentsScreen *screen = context;
    screen->now_ms = now_ms;
    if (listening_module_work.owner != screen) {
        return;
    }
    if (screen->mode == MODE_RESOLVE) {
        StoreResult result = moment_resolve_step(&work->resolve);
        if (result != STORE_WORKING) {
            if (result != STORE_DONE || !play_moment(screen)) {
                message(screen, STR_MOMENT_MISSING);
            }
            show_list(screen);
        }
    } else if (screen->mode == MODE_SAVE || screen->mode == MODE_DELETE) {
        StoreResult result = moments_step(&work->job);
        if (result != STORE_WORKING) {
            message(screen, result == STORE_DONE ? (screen->mode == MODE_SAVE ? STR_MOMENT_SAVED
                                                                              : STR_MOMENT_DELETED)
                                                 : STR_MOMENT_FAILED);
            if (result != STORE_DONE) {
                reload();
            }
            show_list(screen);
        }
    }
    if (working(screen)) {
        wire_motion_run(screen->config.motion, screen, WIRE_SHAPE_CASSETTE, now_ms);
    }
}

static void draw(void *context, Canvas *canvas)
{
    MomentsScreen *screen = context;
    const Theme *theme = screen->config.theme;
    header_draw(canvas, theme, screen->screen.title);
    Rect content = theme_content_area(theme);
    if (working(screen)) {
        message_draw_working(canvas, content,
                             &(MessageConfig){.message = i18n_text(screen->mode == MODE_RESOLVE
                                                                       ? STR_MOMENT_OPENING
                                                                       : STR_MOMENT_WORKING),
                                              .theme = theme,
                                              .motion = screen->config.motion});
        return;
    }
    int hint_height = 2 * font_line_height(theme->fonts.small) + theme->metrics.padding;
    Rect rows = content;
    rows.height -= hint_height;
    list_view_draw(&screen->list, canvas, rows, screen->now_ms);
    Rect hint = {content.x + theme->metrics.padding, rows.y + rows.height,
                 content.width - 2 * theme->metrics.padding, hint_height};
    StringId text = work->load != STORE_EMPTY && work->load != STORE_READY ? STR_MOMENTS_UNAVAILABLE
                    : work->moments.count == 0                             ? STR_MOMENTS_EMPTY
                                                                           : STR_MOMENTS_HINT;
    if (screen->mode != MODE_CONFIRM) {
        label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                           (Label){hint, i18n_text(text)});
    }
}

static void enter(void *context, uint32_t now_ms)
{
    MomentsScreen *screen = context;
    screen->now_ms = now_ms;
    screen->destination = NULL;
    *work = (MomentsWork){0};
    listening_module_work.owner = screen;
    reload();
    show_list(screen);
}

Screen *moments_screen_init(MomentsScreen *screen, MomentsScreenConfig config)
{
    *screen = (MomentsScreen){.config = config};
    screen->model = (ListModel){.context = screen, .count = row_count, .label = row_label};
    screen->screen = (Screen){.title = i18n_text(STR_MOMENTS),
                              .context = screen,
                              .takes_every_key = true,
                              .stays_on_wake = true,
                              .enter = enter,
                              .handle_key = handle_key,
                              .update = update,
                              .draw = draw};
    return &screen->screen;
}
