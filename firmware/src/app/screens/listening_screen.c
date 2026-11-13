#include "listening_screen.h"
#include "listening_work.h"
#include "recap_cards.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "core/utf8.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "playback/listening_stats.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/message_screen.h"
#include "ui/navigation.h"

enum {
    MODE_MENU,
    MODE_TOP,
    MODE_TOTALS,
    MODE_EXPORT,
    MODE_OPTIONS,
    MODE_CONFIRM,
    MODE_PERIODS,
    MODE_RECAP,
    MENU_ROWS = 6,
    RECAP_ROW = 5,
    STAT_GAP = 8,
    EXPORT_MEASURE,
    EXPORT_WRITE,
    EXPORT_DONE,
    EXPORT_EMPTY,
    EXPORT_FAILED,
    EXPORT_STEPS = 16,
    EXPORT_BUDGET_MS = 12,
    PERCENT = 100,
    DETAIL_BYTES = 160
};

static ListeningWork *const work = &listening_module_work.as.listening;
static Toast *toast;
static const bool *scanning;

static const StringId MENU[MENU_ROWS] = {
    STR_TOP_ARTISTS,      STR_TOP_ALBUMS,   STR_TOP_SONGS,
    STR_LISTENING_TOTALS, STR_EXPORT_PLAYS, STR_RECAP,
};

static const StringId PERIODS[LISTENING_PERIOD_COUNT] = {STR_RECAP_THIS_MONTH, STR_RECAP_LAST_MONTH,
                                                         STR_RECAP_THIS_YEAR, STR_RECAP_ALL_TIME};

static StringId title_id(const ListeningScreen *screen)
{
    if (screen->mode == MODE_TOP) {
        return screen->group == LISTENING_BY_ARTIST  ? STR_TOP_ARTISTS
               : screen->group == LISTENING_BY_ALBUM ? STR_TOP_ALBUMS
                                                     : STR_TOP_SONGS;
    }
    if (screen->mode == MODE_OPTIONS || screen->mode == MODE_CONFIRM) {
        return screen->mode == MODE_CONFIRM ? STR_CLEAR_HISTORY : STR_VIEW_OPTIONS;
    }
    if (screen->mode == MODE_PERIODS || screen->mode == MODE_RECAP) {
        return screen->mode == MODE_RECAP ? recap_cards_title(screen) : STR_RECAP;
    }
    return screen->mode == MODE_TOTALS   ? STR_LISTENING_TOTALS
           : screen->mode == MODE_EXPORT ? STR_EXPORT_PLAYS
                                         : STR_LISTENING;
}

void listening_screen_format_time(char *text, size_t capacity, uint64_t millis)
{
    uint64_t minutes = millis / 60000;
    uint32_t hours = minutes / 60 > UINT32_MAX ? UINT32_MAX : (uint32_t)(minutes / 60);
    StringBuilder builder = string_builder_make(text, capacity);
    i18n_format_counts(&builder, STR_LISTENING_HOURS, hours, (uint32_t)(minutes % 60));
}

static int row_count(const void *context)
{
    const ListeningScreen *screen = context;
    if (screen->mode == MODE_OPTIONS || screen->mode == MODE_CONFIRM) {
        return screen->mode == MODE_OPTIONS ? 1 : 3;
    }
    if (screen->mode == MODE_PERIODS || screen->mode == MODE_RECAP) {
        return screen->mode == MODE_PERIODS ? LISTENING_PERIOD_COUNT : 0;
    }
    return screen->mode == MODE_MENU ? MENU_ROWS + (screen->moments != NULL) : work->top.count;
}

static const char *row_label(const void *context, int index)
{
    const ListeningScreen *screen = context;
    if (screen->mode == MODE_OPTIONS) {
        return i18n_text(STR_CLEAR_HISTORY);
    }
    if (screen->mode == MODE_CONFIRM) {
        static const StringId CHOICES[] = {STR_EXPORT_FIRST, STR_CLEAR, STR_CARD_FORMAT_CANCEL};
        return i18n_text(CHOICES[index]);
    }
    if (screen->mode == MODE_PERIODS) {
        return i18n_text(PERIODS[index]);
    }
    return screen->mode == MODE_MENU ? i18n_text(index == MENU_ROWS ? STR_MOMENTS : MENU[index])
                                     : work->names[index];
}

static const char *row_detail(const void *context, int index)
{
    const ListeningScreen *screen = context;
    return screen->mode == MODE_TOP ? work->times[index] : NULL;
}

static bool row_opens(const void *context, int index)
{
    const ListeningScreen *screen = context;
    (void)index;
    return screen->mode == MODE_MENU || screen->mode == MODE_PERIODS;
}

static void begin_job(ListeningScreen *screen)
{
    if (screen->mode == MODE_RECAP) {
        recap_cards_open(screen, screen->now_ms);
        return;
    }
    *work = (ListeningWork){0};
    listening_module_work.owner = screen;
    if (screen->mode == MODE_TOP) {
        listening_top_begin(&work->top, &screen->store->log, screen->group,
                            (ListeningScratch){work->tallies, LISTENING_TALLY_CAPACITY});
    } else if (screen->mode == MODE_EXPORT) {
        ListeningLookup lookup = {listening_store_text, screen->store};
        listening_export_begin(&work->export_job, &screen->store->log, lookup);
        work->export_stage = EXPORT_MEASURE;
    }
}

static void show_mode(ListeningScreen *screen, uint8_t mode)
{
    screen->mode = mode;
    if (mode != MODE_EXPORT) {
        screen->clearing_export = false;
    }
    screen->screen.title = i18n_text(title_id(screen));
    begin_job(screen);
    list_view_init(&screen->list, &screen->model, screen->theme, screen->now_ms);
    if (mode == MODE_CONFIRM) {
        list_view_select(&screen->list, 2, screen->now_ms);
    } else if (mode == MODE_MENU || mode == MODE_PERIODS) {
        list_view_select(&screen->list, mode == MODE_MENU ? screen->selected : screen->period,
                         screen->now_ms);
    }
}

static void show_card(ListeningScreen *screen, int step)
{
    if (recap_cards_turn(screen, step)) {
        screen->screen.title = i18n_text(title_id(screen));
    }
}

static void choose(ListeningScreen *screen)
{
    screen->selected = (uint8_t)screen->list.selected;
    if (screen->selected < 3) {
        screen->group = screen->selected == 0   ? LISTENING_BY_ARTIST
                        : screen->selected == 1 ? LISTENING_BY_ALBUM
                                                : LISTENING_BY_TRACK;
        show_mode(screen, MODE_TOP);
    } else if (screen->selected == RECAP_ROW) {
        show_mode(screen, MODE_PERIODS);
    } else {
        show_mode(screen, screen->selected == 3 ? MODE_TOTALS : MODE_EXPORT);
    }
}

static void cancel_export(const ListeningScreen *screen)
{
    if (screen->mode == MODE_EXPORT && listening_module_work.owner == screen &&
        work->export_stage == EXPORT_WRITE) {
        (void)hal_file_remove(LISTENING_EXPORT_PATH);
        work->export_stage = EXPORT_FAILED;
    }
}

static void notify(const ListeningScreen *screen, StringId message)
{
    if (toast != NULL) {
        toast_show(toast, i18n_text(message), screen->now_ms);
    }
}

static bool clear_allowed(const ListeningScreen *screen)
{
    if (scanning != NULL && *scanning) {
        notify(screen, STR_SCANNING);
        return false;
    }
    if (screen->mode == MODE_EXPORT && work->export_stage <= EXPORT_WRITE) {
        notify(screen, STR_EXPORT_WORKING);
        return false;
    }
    return true;
}

static void clear_history(ListeningScreen *screen)
{
    if (!clear_allowed(screen)) {
        return;
    }
    StoreResult result = listening_store_clear(screen->store);
    if (result == STORE_DONE) {
        show_mode(screen, MODE_MENU);
        notify(screen, STR_HISTORY_CLEARED);
    } else {
        screen->clearing_export = false;
        notify(screen, STR_HISTORY_UNAVAILABLE);
    }
}

static void confirm_clear(ListeningScreen *screen)
{
    if (screen->list.selected == 2) {
        show_mode(screen, MODE_MENU);
    } else if (clear_allowed(screen)) {
        if (screen->list.selected == 0) {
            show_mode(screen, MODE_EXPORT);
            screen->clearing_export = true;
        } else {
            clear_history(screen);
        }
    }
}

static ScreenAction go_back(ListeningScreen *screen)
{
    if (screen->mode == MODE_MENU) {
        return screen_action_pop();
    }
    uint8_t previous = screen->mode == MODE_CONFIRM ? MODE_OPTIONS
                       : screen->mode == MODE_RECAP ? MODE_PERIODS
                                                    : MODE_MENU;
    show_mode(screen, previous);
    return screen_action_none();
}

static void open_period(ListeningScreen *screen)
{
    screen->period = (uint8_t)screen->list.selected;
    screen->card = 0;
    show_mode(screen, MODE_RECAP);
}

static void move(ListeningScreen *screen, int step, uint32_t now_ms)
{
    if (screen->mode == MODE_RECAP) {
        show_card(screen, step);
    } else {
        list_view_move(&screen->list, step, true, now_ms);
    }
}

static ScreenAction select_row(ListeningScreen *screen)
{
    if (screen->mode == MODE_OPTIONS) {
        show_mode(screen, MODE_CONFIRM);
    } else if (screen->mode == MODE_CONFIRM) {
        confirm_clear(screen);
    } else if (screen->mode == MODE_PERIODS) {
        open_period(screen);
    } else if (screen->mode == MODE_RECAP) {
        show_card(screen, 1);
    } else if (screen->mode == MODE_MENU) {
        if (screen->list.selected == MENU_ROWS && screen->moments != NULL) {
            screen->selected = MENU_ROWS;
            return screen_action_push(screen->moments);
        }
        choose(screen);
    }
    return screen_action_none();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    ListeningScreen *screen = context;
    screen->now_ms = now_ms;
    Navigation navigation = navigation_from_key(event);
    if (navigation.intent == NAVIGATION_HOME || navigation.intent == NAVIGATION_BACK) {
        cancel_export(screen);
    }
    if (navigation.intent == NAVIGATION_HOME) {
        return screen_action_pop_to_root();
    }
    if (navigation.intent == NAVIGATION_BACK) {
        return go_back(screen);
    }
    if (navigation.intent == NAVIGATION_OPTIONS) {
        if (clear_allowed(screen)) {
            show_mode(screen, MODE_OPTIONS);
        }
    } else if (navigation.intent == NAVIGATION_NEXT || navigation.intent == NAVIGATION_PREVIOUS) {
        move(screen, navigation.intent == NAVIGATION_NEXT ? 1 : -1, now_ms);
    } else if (navigation.intent == NAVIGATION_SELECT) {
        return select_row(screen);
    }
    return screen_action_none();
}

static void copy_name(char *target, const char *source)
{
    size_t index = 0;
    while (index + 1 < LISTENING_NAME_BYTES && source[index] != '\0') {
        target[index] = source[index];
        index++;
    }
    target[utf8_complete_length(target, index)] = '\0';
}

static void name_top(ListeningScreen *screen)
{
    for (uint8_t index = 0; index < work->top.count; index++) {
        ListeningText text;
        const ListeningRanked *rank = &work->top.top[index];
        bool found = listening_store_text(screen->store, &rank->track, &text);
        const char *name = !found ? i18n_text(STR_HISTORY_UNAVAILABLE)
                           : screen->group == LISTENING_BY_ARTIST ? text.artist
                           : screen->group == LISTENING_BY_ALBUM  ? text.album
                                                                  : text.title;
        copy_name(work->names[index], name);
        listening_screen_format_time(work->times[index], LISTENING_VALUE_BYTES,
                                     rank->tally.listened_ms);
    }
    work->named = true;
    list_view_fit(&screen->list);
}

static void fail_export(StringId reason)
{
    work->export_stage = EXPORT_FAILED;
    work->export_reason = reason;
}

static bool export_file_size(uint32_t *size)
{
    HalFile file;
    if (!hal_file_open(&file, LISTENING_EXPORT_PATH)) {
        return false;
    }
    *size = file.size;
    hal_file_close(&file);
    return true;
}

static bool prepare_export_file(uint32_t bytes)
{
    uint32_t size = 0;
    bool exists = export_file_size(&size);
    if (exists && size != bytes) {
        exists = !hal_file_remove(LISTENING_EXPORT_PATH);
    }
    if (!exists && hal_file_reserve(LISTENING_EXPORT_PATH, bytes) == HAL_RESERVE_NO_SPACE) {
        fail_export(STR_STORAGE_FULL);
        return false;
    }
    if (!export_file_size(&size) || size != bytes) {
        fail_export(STR_EXPORT_WRITE_FAILED);
        return false;
    }
    return true;
}

static void start_export_write(void)
{
    ListeningExport *job = &work->export_job;
    uint32_t bytes =
        job->bytes < LISTENING_EXPORT_FILE_BYTES ? job->bytes : LISTENING_EXPORT_FILE_BYTES;
    if (job->plays == 0) {
        work->export_stage = EXPORT_EMPTY;
    } else if (prepare_export_file(bytes) &&
               listening_export_write(
                   job, listening_store_file(&work->export_file, LISTENING_EXPORT_PATH, bytes))) {
        work->export_stage = EXPORT_WRITE;
    } else if (work->export_stage != EXPORT_FAILED) {
        fail_export(STR_EXPORT_WRITE_FAILED);
    }
}

static void advance_export(void)
{
    StoreResult result = listening_export_step(&work->export_job);
    bool writing = work->export_stage == EXPORT_WRITE;
    if (result == STORE_WORKING) {
        return;
    }
    if (result != STORE_DONE) {
        if (writing) {
            (void)hal_file_remove(LISTENING_EXPORT_PATH);
        }
        fail_export(writing ? STR_EXPORT_WRITE_FAILED : STR_HISTORY_UNAVAILABLE);
    } else if (writing) {
        work->export_stage = EXPORT_DONE;
    } else {
        start_export_write();
    }
}

static void update_export(void)
{
    uint32_t started_ms = hal_clock_milliseconds();
    for (int step = 0; step < EXPORT_STEPS && work->export_stage <= EXPORT_WRITE &&
                       hal_clock_milliseconds() - started_ms < EXPORT_BUDGET_MS;
         step++) {
        advance_export();
        hal_audio_service();
    }
}

static uint32_t export_percent(void)
{
    const ListeningCursor *cursor = &work->export_job.cursor;
    uint32_t kept = cursor->kept;
    uint32_t done = cursor->position + (work->export_stage == EXPORT_WRITE ? kept : 0u);
    return kept == 0 ? 0 : done * PERCENT / (2u * kept);
}

static void follow_motion(ListeningScreen *screen, uint32_t now_ms)
{
    if (screen->mode == MODE_RECAP) {
        recap_cards_motion(screen, now_ms);
        return;
    }
    WireMotion *motion = screen->motion;
    bool exporting = screen->mode == MODE_EXPORT && work->export_stage <= EXPORT_WRITE;
    bool ranking = screen->mode == MODE_TOP && !work->named && work->top.result != STORE_FAILED;
    if (!exporting && !ranking) {
        wire_motion_end(motion, screen);
        return;
    }
    wire_motion_run(motion, screen, exporting ? WIRE_SHAPE_CASSETTE : WIRE_SHAPE_DISC, now_ms);
    if (exporting) {
        wire_motion_progress(motion, export_percent(), PERCENT);
    }
}

static void update(void *context, uint32_t now_ms)
{
    ListeningScreen *screen = context;
    screen->now_ms = now_ms;
    if (listening_module_work.owner != screen) {
        begin_job(screen);
    }
    if (screen->mode == MODE_RECAP) {
        recap_cards_update(screen, now_ms);
        screen->screen.title = i18n_text(title_id(screen));
    } else if (screen->mode == MODE_TOP && !work->named &&
               listening_top_step(&work->top) == STORE_DONE) {
        name_top(screen);
    } else if (screen->mode == MODE_EXPORT) {
        update_export();
        if (screen->clearing_export &&
            (work->export_stage == EXPORT_DONE || work->export_stage == EXPORT_EMPTY)) {
            clear_history(screen);
        }
    }
    follow_motion(screen, now_ms);
    hal_audio_service();
}

static void draw_label(Canvas *canvas, const Theme *theme, Rect area, const char *text, bool strong)
{
    const Font *font = strong ? theme->fonts.title : theme->fonts.small;
    TextStyle style = {.font = font_line_height(font) <= area.height ? font : theme->fonts.small,
                       .color = strong ? theme->colors.text : theme->colors.text_secondary};
    label_draw(canvas, style, (Label){area, text});
}

static void draw_totals(ListeningScreen *screen, Canvas *canvas, Rect area)
{
    const Theme *theme = screen->theme;
    ListeningTotals totals = listening_store_totals(screen->store);
    int padding = theme->metrics.padding;
    int height = (area.height - 4 * STAT_GAP) / 3;
    char value[LISTENING_VALUE_BYTES];
    StringId labels[] = {STR_TOTAL_LISTENING, STR_LISTENING_PLAYS, STR_LISTENING_SKIPS};
    for (int index = 0; index < 3; index++) {
        Rect card = {area.x + padding, area.y + STAT_GAP + index * (height + STAT_GAP),
                     area.width - 2 * padding, height};
        canvas_fill_rect(canvas, card, theme->colors.header_background);
        draw_label(canvas, theme,
                   (Rect){card.x + STAT_GAP, card.y + 3, card.width - 2 * STAT_GAP, 20},
                   i18n_text(labels[index]), false);
        if (index == 0) {
            listening_screen_format_time(value, sizeof value, totals.listened_ms);
        } else {
            StringBuilder builder = string_builder_make(value, sizeof value);
            string_builder_append_unsigned(&builder, index == 1 ? totals.plays : totals.skips);
        }
        draw_label(canvas, theme,
                   (Rect){card.x + STAT_GAP, card.y + 22, card.width - 2 * STAT_GAP, height - 22},
                   value, true);
    }
}

static void history_detail(char *text, size_t capacity)
{
    static const StringId RESETS[] = {STR_LISTENING_TOTALS, STR_TOP_SONGS,   STR_TOP_ALBUMS,
                                      STR_TOP_ARTISTS,      STR_SMART_LISTS, STR_MOST_PLAYED,
                                      STR_RECENTLY_PLAYED,  STR_EXPORT_PLAYS};
    char resets[384];
    char keeps[128];
    StringBuilder builder = string_builder_make(resets, sizeof resets);
    for (size_t index = 0; index < sizeof RESETS / sizeof RESETS[0]; index++) {
        if (index != 0) {
            string_builder_append_char(&builder, index == 4 || index == 7 ? '\n' : '/');
        }
        string_builder_append(&builder, i18n_text(RESETS[index]));
    }
    builder = string_builder_make(keeps, sizeof keeps);
    string_builder_append(&builder, i18n_text(STR_MOMENTS));
    string_builder_append_char(&builder, '/');
    string_builder_append(&builder, i18n_text(STR_FAVORITES));
    const char *arguments[] = {resets, keeps};
    builder = string_builder_make(text, capacity);
    i18n_format(&builder, STR_HISTORY_RESET, arguments, 2);
}

static void draw_confirmation(ListeningScreen *screen, Canvas *canvas, Rect area)
{
    const Theme *theme = screen->theme;
    int padding = theme->metrics.padding;
    char text[640];
    history_detail(text, sizeof text);
    Theme choices = *theme;
    choices.fonts.body = theme->fonts.small;
    Rect rows = area;
    Rect detail = {area.x + padding, area.y + padding, area.width - 2 * padding,
                   area.height - 2 * padding};
    if (theme_is_portrait(theme)) {
        rows.height = 3 * theme->metrics.row_height;
        rows.y = area.y + area.height - rows.height;
        detail.height = rows.y - detail.y - padding;
    } else {
        rows.width = area.width * 9 / 20;
        rows.x = area.x + area.width - rows.width;
        detail.width = rows.x - detail.x - padding;
    }
    label_draw_wrapped(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
                       (Label){detail, text});
    screen->list.theme = &choices;
    list_view_fit_rows(&screen->list, rows.height / choices.metrics.row_height);
    list_view_draw(&screen->list, canvas, rows, screen->now_ms);
    screen->list.theme = theme;
}

static void draw_menu(ListeningScreen *screen, Canvas *canvas, Rect content)
{
    const Theme *theme = screen->theme;
    bool portrait = theme_is_portrait(theme);
    int hint = font_line_height(theme->fonts.small);
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
               (Label){{content.x + theme->metrics.padding, content.y + content.height - hint,
                        content.width - 2 * theme->metrics.padding, hint},
                       i18n_text(STR_LISTENING_OPTIONS_HINT)});
    content.height -= hint;
    Rect rows = content;
    rows.height = portrait ? theme->metrics.row_height * (MENU_ROWS + (screen->moments != NULL))
                           : content.height;
    rows.width = portrait ? content.width : content.width * 3 / 5;
    Rect panel =
        portrait
            ? (Rect){content.x, rows.y + rows.height, content.width, content.height - rows.height}
            : (Rect){rows.x + rows.width, content.y, content.width - rows.width, content.height};
    list_view_fit_rows(&screen->list, rows.height / theme->metrics.row_height);
    list_view_draw(&screen->list, canvas, rows, screen->now_ms);
    draw_totals(screen, canvas, panel);
}

static void write_saved(StringBuilder *builder)
{
    char count[LISTENING_VALUE_BYTES];
    StringBuilder number = string_builder_make(count, sizeof count);
    uint16_t exported = work->export_job.exported;
    string_builder_append_unsigned(&number, exported);
    const char *arguments[] = {count, LISTENING_EXPORT_PATH};
    i18n_format(builder, exported == 1 ? STR_EXPORT_ONE_SAVED : STR_EXPORT_SAVED, arguments, 2);
}

static void write_export_detail(StringBuilder *builder)
{
    uint8_t stage = work->export_stage;
    if (stage <= EXPORT_WRITE) {
        i18n_format_counts(builder, STR_EXPORT_PROGRESS, export_percent(), 0);
    } else if (stage == EXPORT_DONE) {
        write_saved(builder);
    } else if (stage == EXPORT_EMPTY) {
        string_builder_append(
            builder,
            i18n_text(work->export_job.unknown > 0 ? STR_EXPORT_UNKNOWN : STR_LISTENING_HINT));
    } else if (work->export_reason == STR_EXPORT_WRITE_FAILED) {
        i18n_format_text(builder, STR_EXPORT_WRITE_FAILED, LISTENING_EXPORT_PATH);
    } else {
        string_builder_append(builder, i18n_text(work->export_reason));
    }
}

static int wrapped_height(const Font *font, const char *text, int width)
{
    return label_wrap_count(font, text, width) * font_line_height(font);
}

static int draw_motion_above(Canvas *canvas, const Theme *theme, Rect column, WireMotion *motion)
{
    Rect space = {column.x, column.y, column.width, column.height - STAT_GAP};
    int side = motion != NULL ? wire_motion_fit(space).width : 0;
    int top = column.y + (column.height - (side > 0 ? side + STAT_GAP : 0)) / 2;
    if (side > 0) {
        wire_motion_draw(motion, canvas, theme, (Rect){column.x, top, column.width, side});
        top += side + STAT_GAP;
    }
    return top;
}

void listening_screen_draw_status(const Theme *theme, Canvas *canvas, Rect area,
                                  ExportStatus status)
{
    const Font *title = theme->fonts.title;
    const Font *body = theme->fonts.body;
    int padding = theme->metrics.padding;
    Rect inner = {area.x + padding, area.y, area.width - 2 * padding, area.height - padding};
    int hint = status.hint != NULL ? font_line_height(theme->fonts.small) : 0;
    int headline = wrapped_height(title, status.headline, inner.width);
    int detail = wrapped_height(body, status.detail, inner.width);
    Rect column = {inner.x, inner.y, inner.width,
                   inner.height - hint - headline - STAT_GAP - detail};
    int top = draw_motion_above(canvas, theme, column, status.motion);
    label_draw_wrapped(canvas, (TextStyle){title, theme->colors.text, false},
                       (Label){{inner.x, top, inner.width, headline}, status.headline});
    label_draw_wrapped(
        canvas, (TextStyle){body, theme->colors.text_secondary, false},
        (Label){{inner.x, top + headline + STAT_GAP, inner.width, detail}, status.detail});
    if (status.hint != NULL) {
        label_draw_wrapped(
            canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
            (Label){{inner.x, inner.y + inner.height - hint, inner.width, hint}, status.hint});
    }
}

static void draw_export(const ListeningScreen *screen, Canvas *canvas, Rect area)
{
    const Theme *theme = screen->theme;
    static const StringId HEADLINES[] = {STR_EXPORT_WORKING, STR_EXPORT_WORKING, STR_EXPORT_DONE,
                                         STR_EXPORT_NONE, STR_EXPORT_FAILED};
    char detail[DETAIL_BYTES];
    StringBuilder builder = string_builder_make(detail, sizeof detail);
    uint8_t stage = work->export_stage < EXPORT_MEASURE ? EXPORT_MEASURE : work->export_stage;
    write_export_detail(&builder);
    bool working = stage <= EXPORT_WRITE;
    listening_screen_draw_status(theme, canvas, area,
                                 (ExportStatus){i18n_text(HEADLINES[stage - EXPORT_MEASURE]),
                                                detail,
                                                working ? i18n_text(STR_BACK_CANCELS) : NULL,
                                                working ? screen->motion : NULL});
}

static void draw(void *context, Canvas *canvas)
{
    ListeningScreen *screen = context;
    const Theme *theme = screen->theme;
    Rect content = theme_content_area(theme);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, screen->screen.title);
    if (screen->mode == MODE_CONFIRM) {
        draw_confirmation(screen, canvas, content);
    } else if (screen->mode == MODE_OPTIONS || screen->mode == MODE_PERIODS) {
        list_view_draw(&screen->list, canvas, content, screen->now_ms);
    } else if (screen->mode == MODE_RECAP) {
        recap_cards_draw(screen, canvas, content);
    } else if (screen->mode == MODE_MENU) {
        draw_menu(screen, canvas, content);
    } else if (screen->mode == MODE_TOTALS) {
        draw_totals(screen, canvas, content);
    } else if (screen->mode == MODE_EXPORT) {
        draw_export(screen, canvas, content);
    } else if (work->top.result == STORE_FAILED) {
        message_draw(canvas, theme, content, i18n_text(STR_HISTORY_UNAVAILABLE));
    } else if (!work->named) {
        MessageConfig working = {
            .message = i18n_text(STR_LISTENING_WORKING), .theme = theme, .motion = screen->motion};
        message_draw_working(canvas, content, &working);
    } else if (work->top.count == 0) {
        message_draw(canvas, theme, content, i18n_text(STR_LISTENING_EMPTY));
    } else {
        list_view_draw(&screen->list, canvas, content, screen->now_ms);
    }
}

static void enter(void *context, uint32_t now_ms)
{
    ListeningScreen *screen = context;
    screen->now_ms = now_ms;
    show_mode(screen, MODE_MENU);
}

Screen *listening_screen_init(ListeningScreen *screen, ListeningScreenConfig config)
{
    *screen =
        (ListeningScreen){.store = config.store, .theme = config.theme, .motion = config.motion};
    toast = config.toast;
    scanning = config.scanning;
    recap_cards_bind(config.covers, config.settings);
    screen->model = (ListModel){.context = screen,
                                .count = row_count,
                                .label = row_label,
                                .detail = row_detail,
                                .opens = row_opens,
                                .detail_max_width = 96};
    screen->screen = (Screen){.title = i18n_text(STR_LISTENING),
                              .context = screen,
                              .enter = enter,
                              .handle_key = handle_key,
                              .update = update,
                              .draw = draw};
    return &screen->screen;
}
