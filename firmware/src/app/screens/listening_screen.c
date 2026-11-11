#include "listening_screen.h"

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
    MENU_ROWS = 5,
    NAME_BYTES = 128,
    VALUE_BYTES = 32,
    TALLY_CAPACITY = 512,
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

typedef struct ExportStatus {
    const char *headline;
    const char *detail;
    const char *hint;
} ExportStatus;

typedef struct ListeningWork {
    ListeningScreen *owner;
    ListeningTop top;
    ListeningExport export_job;
    ListeningFile export_file;
    ListeningTally tallies[TALLY_CAPACITY];
    char names[LISTENING_TOP_COUNT][NAME_BYTES];
    char times[LISTENING_TOP_COUNT][VALUE_BYTES];
    StringId export_reason;
    uint8_t export_stage;
    bool named;
} ListeningWork;

static ListeningWork work;

static const StringId MENU[MENU_ROWS] = {
    STR_TOP_ARTISTS, STR_TOP_ALBUMS, STR_TOP_SONGS, STR_LISTENING_TOTALS, STR_EXPORT_PLAYS,
};

static StringId title_id(const ListeningScreen *screen)
{
    if (screen->mode == MODE_TOP) {
        return screen->group == LISTENING_BY_ARTIST  ? STR_TOP_ARTISTS
               : screen->group == LISTENING_BY_ALBUM ? STR_TOP_ALBUMS
                                                     : STR_TOP_SONGS;
    }
    return screen->mode == MODE_TOTALS   ? STR_LISTENING_TOTALS
           : screen->mode == MODE_EXPORT ? STR_EXPORT_PLAYS
                                         : STR_LISTENING;
}

static void format_time(char *text, size_t capacity, uint64_t millis)
{
    uint64_t minutes = millis / 60000;
    uint32_t hours = minutes / 60 > UINT32_MAX ? UINT32_MAX : (uint32_t)(minutes / 60);
    StringBuilder builder = string_builder_make(text, capacity);
    i18n_format_counts(&builder, STR_LISTENING_HOURS, hours, (uint32_t)(minutes % 60));
}

static int row_count(const void *context)
{
    const ListeningScreen *screen = context;
    return screen->mode == MODE_MENU ? MENU_ROWS : work.top.count;
}

static const char *row_label(const void *context, int index)
{
    const ListeningScreen *screen = context;
    return screen->mode == MODE_MENU ? i18n_text(MENU[index]) : work.names[index];
}

static const char *row_detail(const void *context, int index)
{
    const ListeningScreen *screen = context;
    return screen->mode == MODE_TOP ? work.times[index] : NULL;
}

static bool row_opens(const void *context, int index)
{
    const ListeningScreen *screen = context;
    (void)index;
    return screen->mode == MODE_MENU;
}

static void begin_job(ListeningScreen *screen)
{
    work = (ListeningWork){.owner = screen};
    if (screen->mode == MODE_TOP) {
        listening_top_begin(&work.top, &screen->config.store->log, screen->group,
                            (ListeningScratch){work.tallies, TALLY_CAPACITY});
    } else if (screen->mode == MODE_EXPORT) {
        ListeningLookup lookup = {listening_store_text, screen->config.store};
        listening_export_begin(&work.export_job, &screen->config.store->log, lookup);
        work.export_stage = EXPORT_MEASURE;
    }
}

static void show_mode(ListeningScreen *screen, uint8_t mode)
{
    screen->mode = mode;
    screen->screen.title = i18n_text(title_id(screen));
    begin_job(screen);
    list_view_init(&screen->list, &screen->model, screen->config.theme, screen->now_ms);
    if (mode == MODE_MENU) {
        list_view_select(&screen->list, screen->selected, screen->now_ms);
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
    } else {
        show_mode(screen, screen->selected == 3 ? MODE_TOTALS : MODE_EXPORT);
    }
}

static void cancel_export(const ListeningScreen *screen)
{
    if (screen->mode == MODE_EXPORT && work.owner == screen && work.export_stage == EXPORT_WRITE) {
        (void)hal_file_remove(LISTENING_EXPORT_PATH);
        work.export_stage = EXPORT_FAILED;
    }
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
        if (screen->mode == MODE_MENU) {
            return screen_action_pop();
        }
        show_mode(screen, MODE_MENU);
    } else if (navigation.intent == NAVIGATION_NEXT || navigation.intent == NAVIGATION_PREVIOUS) {
        list_view_move(&screen->list, navigation.intent == NAVIGATION_NEXT ? 1 : -1, true, now_ms);
    } else if (navigation.intent == NAVIGATION_SELECT && screen->mode == MODE_MENU) {
        choose(screen);
    }
    return screen_action_none();
}

static void copy_name(char *target, const char *source)
{
    size_t index = 0;
    while (index + 1 < NAME_BYTES && source[index] != '\0') {
        target[index] = source[index];
        index++;
    }
    target[utf8_complete_length(target, index)] = '\0';
}

static void name_top(ListeningScreen *screen)
{
    for (uint8_t index = 0; index < work.top.count; index++) {
        ListeningText text;
        const ListeningRanked *rank = &work.top.top[index];
        bool found = listening_store_text(screen->config.store, &rank->track, &text);
        const char *name = !found ? i18n_text(STR_HISTORY_UNAVAILABLE)
                           : screen->group == LISTENING_BY_ARTIST ? text.artist
                           : screen->group == LISTENING_BY_ALBUM  ? text.album
                                                                  : text.title;
        copy_name(work.names[index], name);
        format_time(work.times[index], VALUE_BYTES, rank->tally.listened_ms);
    }
    work.named = true;
    list_view_fit(&screen->list);
}

static void fail_export(StringId reason)
{
    work.export_stage = EXPORT_FAILED;
    work.export_reason = reason;
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
    ListeningExport *job = &work.export_job;
    uint32_t bytes =
        job->bytes < LISTENING_EXPORT_FILE_BYTES ? job->bytes : LISTENING_EXPORT_FILE_BYTES;
    if (job->plays == 0) {
        work.export_stage = EXPORT_EMPTY;
    } else if (prepare_export_file(bytes) &&
               listening_export_write(
                   job, listening_store_file(&work.export_file, LISTENING_EXPORT_PATH, bytes))) {
        work.export_stage = EXPORT_WRITE;
    } else if (work.export_stage != EXPORT_FAILED) {
        fail_export(STR_EXPORT_WRITE_FAILED);
    }
}

static void advance_export(void)
{
    StoreResult result = listening_export_step(&work.export_job);
    bool writing = work.export_stage == EXPORT_WRITE;
    if (result == STORE_WORKING) {
        return;
    }
    if (result != STORE_DONE) {
        if (writing) {
            (void)hal_file_remove(LISTENING_EXPORT_PATH);
        }
        fail_export(writing ? STR_EXPORT_WRITE_FAILED : STR_HISTORY_UNAVAILABLE);
    } else if (writing) {
        work.export_stage = EXPORT_DONE;
    } else {
        start_export_write();
    }
}

static void update_export(void)
{
    uint32_t started_ms = hal_clock_milliseconds();
    for (int step = 0; step < EXPORT_STEPS && work.export_stage <= EXPORT_WRITE &&
                       hal_clock_milliseconds() - started_ms < EXPORT_BUDGET_MS;
         step++) {
        advance_export();
        hal_audio_service();
    }
}

static void update(void *context, uint32_t now_ms)
{
    ListeningScreen *screen = context;
    screen->now_ms = now_ms;
    if (work.owner != screen) {
        begin_job(screen);
    }
    if (screen->mode == MODE_TOP && !work.named && listening_top_step(&work.top) == STORE_DONE) {
        name_top(screen);
    } else if (screen->mode == MODE_EXPORT) {
        update_export();
    }
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
    const Theme *theme = screen->config.theme;
    ListeningTotals totals = listening_store_totals(screen->config.store);
    int padding = theme->metrics.padding;
    int height = (area.height - 4 * STAT_GAP) / 3;
    char value[VALUE_BYTES];
    StringId labels[] = {STR_TOTAL_LISTENING, STR_LISTENING_PLAYS, STR_LISTENING_SKIPS};
    for (int index = 0; index < 3; index++) {
        Rect card = {area.x + padding, area.y + STAT_GAP + index * (height + STAT_GAP),
                     area.width - 2 * padding, height};
        canvas_fill_rect(canvas, card, theme->colors.header_background);
        draw_label(canvas, theme,
                   (Rect){card.x + STAT_GAP, card.y + 3, card.width - 2 * STAT_GAP, 20},
                   i18n_text(labels[index]), false);
        if (index == 0) {
            format_time(value, sizeof value, totals.listened_ms);
        } else {
            StringBuilder builder = string_builder_make(value, sizeof value);
            string_builder_append_unsigned(&builder, index == 1 ? totals.plays : totals.skips);
        }
        draw_label(canvas, theme,
                   (Rect){card.x + STAT_GAP, card.y + 22, card.width - 2 * STAT_GAP, height - 22},
                   value, true);
    }
}

static void draw_menu(ListeningScreen *screen, Canvas *canvas, Rect content)
{
    const Theme *theme = screen->config.theme;
    bool portrait = theme_is_portrait(theme);
    Rect rows = content;
    rows.height = portrait ? theme->metrics.row_height * MENU_ROWS : content.height;
    rows.width = portrait ? content.width : content.width * 3 / 5;
    Rect panel =
        portrait
            ? (Rect){content.x, rows.y + rows.height, content.width, content.height - rows.height}
            : (Rect){rows.x + rows.width, content.y, content.width - rows.width, content.height};
    list_view_fit_rows(&screen->list, rows.height / theme->metrics.row_height);
    list_view_draw(&screen->list, canvas, rows, screen->now_ms);
    draw_totals(screen, canvas, panel);
}

static uint32_t export_percent(void)
{
    const ListeningCursor *cursor = &work.export_job.cursor;
    uint32_t kept = cursor->kept;
    uint32_t done = cursor->position + (work.export_stage == EXPORT_WRITE ? kept : 0u);
    return kept == 0 ? 0 : done * PERCENT / (2u * kept);
}

static void write_saved(StringBuilder *builder)
{
    char count[VALUE_BYTES];
    StringBuilder number = string_builder_make(count, sizeof count);
    uint16_t exported = work.export_job.exported;
    string_builder_append_unsigned(&number, exported);
    const char *arguments[] = {count, LISTENING_EXPORT_PATH};
    i18n_format(builder, exported == 1 ? STR_EXPORT_ONE_SAVED : STR_EXPORT_SAVED, arguments, 2);
}

static void write_export_detail(StringBuilder *builder)
{
    uint8_t stage = work.export_stage;
    if (stage <= EXPORT_WRITE) {
        i18n_format_counts(builder, STR_EXPORT_PROGRESS, export_percent(), 0);
    } else if (stage == EXPORT_DONE) {
        write_saved(builder);
    } else if (stage == EXPORT_EMPTY) {
        string_builder_append(builder, i18n_text(work.export_job.unknown > 0 ? STR_EXPORT_UNKNOWN
                                                                             : STR_LISTENING_HINT));
    } else if (work.export_reason == STR_EXPORT_WRITE_FAILED) {
        i18n_format_text(builder, STR_EXPORT_WRITE_FAILED, LISTENING_EXPORT_PATH);
    } else {
        string_builder_append(builder, i18n_text(work.export_reason));
    }
}

static int wrapped_height(const Font *font, const char *text, int width)
{
    return label_wrap_count(font, text, width) * font_line_height(font);
}

static void draw_status(const Theme *theme, Canvas *canvas, Rect area, ExportStatus status)
{
    const Font *title = theme->fonts.title;
    const Font *body = theme->fonts.body;
    int padding = theme->metrics.padding;
    Rect inner = {area.x + padding, area.y, area.width - 2 * padding, area.height - padding};
    int hint = status.hint != NULL ? font_line_height(theme->fonts.small) : 0;
    int headline = wrapped_height(title, status.headline, inner.width);
    int detail = wrapped_height(body, status.detail, inner.width);
    int top = inner.y + (inner.height - hint - headline - STAT_GAP - detail) / 2;
    label_draw_wrapped(canvas, (TextStyle){title, theme->colors.text},
                       (Label){{inner.x, top, inner.width, headline}, status.headline});
    label_draw_wrapped(
        canvas, (TextStyle){body, theme->colors.text_secondary},
        (Label){{inner.x, top + headline + STAT_GAP, inner.width, detail}, status.detail});
    if (status.hint != NULL) {
        label_draw_wrapped(
            canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
            (Label){{inner.x, inner.y + inner.height - hint, inner.width, hint}, status.hint});
    }
}

static void draw_export(const Theme *theme, Canvas *canvas, Rect area)
{
    static const StringId HEADLINES[] = {STR_EXPORT_WORKING, STR_EXPORT_WORKING, STR_EXPORT_DONE,
                                         STR_EXPORT_NONE, STR_EXPORT_FAILED};
    char detail[DETAIL_BYTES];
    StringBuilder builder = string_builder_make(detail, sizeof detail);
    uint8_t stage = work.export_stage < EXPORT_MEASURE ? EXPORT_MEASURE : work.export_stage;
    write_export_detail(&builder);
    draw_status(theme, canvas, area,
                (ExportStatus){i18n_text(HEADLINES[stage - EXPORT_MEASURE]), detail,
                               stage <= EXPORT_WRITE ? i18n_text(STR_BACK_CANCELS) : NULL});
}

static void draw(void *context, Canvas *canvas)
{
    ListeningScreen *screen = context;
    const Theme *theme = screen->config.theme;
    Rect content = theme_content_area(theme);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, screen->screen.title);
    if (screen->mode == MODE_MENU) {
        draw_menu(screen, canvas, content);
    } else if (screen->mode == MODE_TOTALS) {
        draw_totals(screen, canvas, content);
    } else if (screen->mode == MODE_EXPORT) {
        draw_export(theme, canvas, content);
    } else if (work.top.result == STORE_FAILED) {
        message_draw(canvas, theme, content, i18n_text(STR_HISTORY_UNAVAILABLE));
    } else if (!work.named) {
        message_draw(canvas, theme, content, i18n_text(STR_LISTENING_WORKING));
    } else if (work.top.count == 0) {
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
    *screen = (ListeningScreen){.config = config};
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
