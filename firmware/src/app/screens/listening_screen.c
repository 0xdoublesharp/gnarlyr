#include "listening_screen.h"

#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "core/utf8.h"
#include "hal/audio.h"
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
    EXPORT_FINISHED
};

typedef struct ListeningWork {
    ListeningScreen *owner;
    ListeningTop top;
    ListeningExport export_job;
    ListeningFile export_file;
    ListeningTally tallies[TALLY_CAPACITY];
    char names[LISTENING_TOP_COUNT][NAME_BYTES];
    char times[LISTENING_TOP_COUNT][VALUE_BYTES];
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

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    ListeningScreen *screen = context;
    screen->now_ms = now_ms;
    Navigation navigation = navigation_from_key(event);
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

static void update_export(ListeningScreen *screen)
{
    StoreResult result = listening_export_step(&work.export_job);
    if (result != STORE_DONE || work.export_stage != EXPORT_MEASURE) {
        return;
    }
    HalReserveResult reserved =
        hal_file_reserve(LISTENING_EXPORT_PATH, LISTENING_EXPORT_FILE_BYTES);
    if ((reserved != HAL_RESERVE_CREATED && reserved != HAL_RESERVE_EXISTS) ||
        !listening_export_write(&work.export_job,
                                listening_store_file(&work.export_file, LISTENING_EXPORT_PATH,
                                                     LISTENING_EXPORT_FILE_BYTES))) {
        work.export_job.result = STORE_FAILED;
    }
    work.export_stage = EXPORT_WRITE;
    (void)screen;
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
        update_export(screen);
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
        StoreResult result = work.export_job.result;
        StringId message =
            result == STORE_DONE && work.export_stage == EXPORT_WRITE ? STR_EXPORT_DONE
            : result == STORE_FAILED || result == STORE_UNCERTAIN     ? STR_EXPORT_FAILED
                                                                      : STR_EXPORT_WORKING;
        message_draw(canvas, theme, content, i18n_text(message));
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
