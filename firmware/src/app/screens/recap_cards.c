#include "recap_cards.h"

#include <stddef.h>

#include "core/calendar.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "core/utf8.h"
#include "gfx/image.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "home_art.h"
#include "listening_work.h"
#include "ui/icons.h"
#include "ui/label.h"
#include "ui/message_screen.h"
#include "wall_time.h"

enum {
    STAGE_WORKING,
    STAGE_NAMING,
    STAGE_COVERS,
    STAGE_READY,
    STAGE_FAILED,
    STAGE_NO_CLOCK
};

enum {
    CARD_TITLE,
    CARD_TIME,
    CARD_ARTISTS,
    CARD_ALBUMS,
    CARD_SONGS,
    CARD_RHYTHM,
    CARD_SOUND,
    CARD_CLOSING,
    CARD_COUNT
};

enum {
    STEPS = 64,
    BUDGET_MS = 12,
    GAP = 8,
    BAR = 3,
    RULE = 2,
    VALUE_BYTES = 48,
    TILE_LIMIT = 4,
    SHARES = 3,
    ALBUM_GROUP = 1,
    COVER_POSITION = 0x7FFF0000,
    NO_ALBUM = 0xFFFF,
    PERCENT = 100,
    HALF_DAY_HOURS = 12,
    TEN = 10
};

typedef struct Pane {
    Canvas *canvas;
    const Theme *theme;
    Rect area;
} Pane;

typedef struct Tile {
    StringId label;
    const char *value;
} Tile;

typedef struct RankRow {
    Rect area;
    uint8_t group;
    uint8_t index;
    bool stacked;
} RankRow;

static RecapWork *const work = &listening_module_work.as.recap;
static CoverCache *covers;
static const Settings *settings;

static const StringId PERIOD_TITLES[LISTENING_PERIOD_COUNT] = {
    STR_RECAP_YOUR_MONTH, STR_RECAP_LAST_MONTH, STR_RECAP_YOUR_YEAR, STR_RECAP_ALL_TIME};

static const StringId CARD_TITLES[CARD_COUNT] = {
    STR_RECAP,     STR_TOTAL_LISTENING, STR_TOP_ARTISTS, STR_TOP_ALBUMS,
    STR_TOP_SONGS, STR_RECAP_RHYTHM,    STR_RECAP_SOUND, STR_RECAP_HIGHLIGHTS};

void recap_cards_bind(CoverCache *cache, const Settings *chosen)
{
    covers = cache;
    settings = chosen;
}

static const ListeningRecap *recap(void)
{
    return &work->job.recap;
}

static bool card_shown(uint8_t card)
{
    const ListeningRecap *found = recap();
    if (card >= CARD_ARTISTS && card <= CARD_SONGS) {
        return found->counts[card - CARD_ARTISTS] > 0;
    }
    if (card == CARD_RHYTHM) {
        return found->weekday != LISTENING_RECAP_NONE || found->artists > 0;
    }
    return card != CARD_SOUND || found->totals.listened_ms > 0;
}

static bool deck_ready(const ListeningScreen *screen)
{
    return listening_module_work.owner == screen && work->stage == STAGE_READY &&
           recap()->totals.plays > 0;
}

uint8_t recap_cards_count(const ListeningScreen *screen)
{
    uint8_t count = 0;
    for (uint8_t card = 0; card < CARD_COUNT && deck_ready(screen); card++) {
        count = (uint8_t)(count + (card_shown(card) ? 1 : 0));
    }
    return count;
}

static uint8_t card_at(uint8_t place)
{
    for (uint8_t card = 0; card < CARD_COUNT; card++) {
        if (card_shown(card) && place-- == 0) {
            return card;
        }
    }
    return CARD_CLOSING;
}

static uint8_t current_card(const ListeningScreen *screen)
{
    return deck_ready(screen) ? card_at(screen->card) : CARD_COUNT;
}

StringId recap_cards_title(const ListeningScreen *screen)
{
    uint8_t card = current_card(screen);
    return card < CARD_COUNT ? CARD_TITLES[card] : STR_RECAP;
}

bool recap_cards_turn(ListeningScreen *screen, int step)
{
    int count = recap_cards_count(screen);
    int card = screen->card + step;
    if (card < 0 || card >= count) {
        return false;
    }
    screen->card = (uint8_t)card;
    return true;
}

static StoreFile cache_file(void)
{
    return listening_store_file(&work->cache, LISTENING_RECAP_PATH, LISTENING_RECAP_FILE_BYTES);
}

void recap_cards_open(ListeningScreen *screen, uint32_t now_ms)
{
    (void)now_ms;
    *work = (RecapWork){.stage = STAGE_WORKING};
    listening_module_work.owner = screen;
    WallNow now = wall_time_now();
    ListeningPeriod period = (ListeningPeriod)screen->period;
    ListeningSpan span;
    if (!listening_period_span(period, now.known ? now.local_s : 0, &span)) {
        work->stage = STAGE_NO_CLOCK;
        return;
    }
    StoreFile file = cache_file();
    work->job.recap.span = span;
    if (listening_recap_load(&file, &screen->store->log, period, &work->job.recap)) {
        work->stage = STAGE_NAMING;
        return;
    }
    work->log = screen->store->log;
    listening_recap_begin(&work->job, &work->log, span,
                          (ListeningScratch){work->tallies, LISTENING_TALLY_CAPACITY});
}

static void save(const ListeningScreen *screen)
{
    HalReserveResult reserved = hal_file_reserve(LISTENING_RECAP_PATH, LISTENING_RECAP_FILE_BYTES);
    if (reserved == HAL_RESERVE_EXISTS || reserved == HAL_RESERVE_CREATED) {
        StoreFile file = cache_file();
        (void)listening_recap_save(&file, (ListeningPeriod)screen->period, &work->job);
    }
}

static bool read_open(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const HalFile *file = context;
    bool read = offset <= file->size && length <= file->size - offset &&
                hal_file_read(*file, offset, bytes, length);
    hal_system_keep_alive();
    return read;
}

static bool open_log(const ListeningScreen *screen)
{
    const char *path = screen->store->file.path;
    if (path == NULL || !hal_file_open(&work->handle, path)) {
        return false;
    }
    work->log.file.source = (ByteSource){read_open, &work->handle};
    return true;
}

static void close_log(const ListeningScreen *screen)
{
    hal_file_close(&work->handle);
    work->log.file = screen->store->log.file;
}

static void compute(const ListeningScreen *screen)
{
    bool opened = open_log(screen);
    uint32_t started_ms = hal_clock_milliseconds();
    for (int step = 0; step < STEPS && work->job.result == STORE_WORKING &&
                       hal_clock_milliseconds() - started_ms < BUDGET_MS;
         step++) {
        listening_recap_step(&work->job);
        hal_audio_service();
    }
    if (opened) {
        close_log(screen);
    }
    if (work->job.result == STORE_DONE) {
        save(screen);
        work->stage = STAGE_NAMING;
    } else if (work->job.result != STORE_WORKING) {
        work->stage = STAGE_FAILED;
    }
}

static void copy_text(char *target, const char *source)
{
    size_t index = 0;
    while (index + 1 < RECAP_NAME_BYTES && source[index] != '\0') {
        target[index] = source[index];
        index++;
    }
    target[utf8_complete_length(target, index)] = '\0';
}

static uint16_t album_index(const ListeningScreen *screen, const ListeningTrack *track)
{
    LibraryTrack record;
    return library_read_track(screen->store->library, track->index, &record) ? record.album
                                                                             : NO_ALBUM;
}

static void name_entry(const ListeningScreen *screen, uint8_t group, uint8_t index)
{
    const ListeningRecapEntry *entry = &recap()->top[group][index];
    ListeningGroup kind = LISTENING_RECAP_ORDER[group];
    ListeningText text;
    bool found = listening_store_text(screen->store, &entry->track, &text);
    const char *name = !found                        ? i18n_text(STR_HISTORY_UNAVAILABLE)
                       : kind == LISTENING_BY_ARTIST ? text.artist
                       : kind == LISTENING_BY_ALBUM  ? text.album
                                                     : text.title;
    copy_text(work->names[group][index], name);
    if (group > 0) {
        copy_text(work->artists[group - 1][index], found ? text.artist : "");
    }
    if (kind == LISTENING_BY_ALBUM) {
        work->albums[index] = found ? album_index(screen, &entry->track) : NO_ALBUM;
    }
}

static void name_all(const ListeningScreen *screen)
{
    for (uint8_t group = 0; group < LISTENING_RECAP_GROUPS; group++) {
        for (uint8_t index = 0; index < recap()->counts[group]; index++) {
            name_entry(screen, group, index);
        }
    }
    work->stage = STAGE_COVERS;
}

static void load_cover(void)
{
    while (work->covers < recap()->counts[ALBUM_GROUP]) {
        uint16_t album = work->albums[work->covers];
        int position = COVER_POSITION + work->covers;
        work->covers++;
        if (covers != NULL && album != NO_ALBUM && cover_cache_load_half(covers, position, album)) {
            return;
        }
    }
    work->stage = STAGE_READY;
}

void recap_cards_update(ListeningScreen *screen, uint32_t now_ms)
{
    if (listening_module_work.owner != screen) {
        recap_cards_open(screen, now_ms);
    }
    if (work->stage == STAGE_WORKING) {
        compute(screen);
    } else if (work->stage == STAGE_NAMING) {
        name_all(screen);
    } else if (work->stage == STAGE_COVERS) {
        load_cover();
    }
    uint8_t count = recap_cards_count(screen);
    if (count > 0 && screen->card >= count) {
        screen->card = (uint8_t)(count - 1);
    }
}

static WireMotion still_figure(const ListeningScreen *screen)
{
    return (WireMotion){.owner = screen,
                        .turn = (uint8_t)(RECAP_FLOURISH_MS / WIRE_MOTION_TURN_MS),
                        .shape = WIRE_SHAPE_ICOSAHEDRON,
                        .percent = PERCENT,
                        .running = true,
                        .ticked = true};
}

static void flourish(ListeningScreen *screen, uint32_t now_ms)
{
    WireMotion *motion = screen->motion;
    bool mine = motion->owner == screen && motion->running;
    if (mine && motion->shape != WIRE_SHAPE_ICOSAHEDRON) {
        wire_motion_end(motion, screen);
        return;
    }
    if (screen->card != 0 || (mine && now_ms - motion->started_ms >= RECAP_FLOURISH_MS)) {
        work->flourished = true;
        work->still = still_figure(screen);
        wire_motion_end(motion, screen);
        return;
    }
    wire_motion_run(motion, screen, WIRE_SHAPE_ICOSAHEDRON, now_ms);
    wire_motion_progress(motion, now_ms - motion->started_ms, RECAP_FLOURISH_MS);
}

void recap_cards_motion(ListeningScreen *screen, uint32_t now_ms)
{
    bool working = listening_module_work.owner == screen && work->stage < STAGE_READY;
    if (working) {
        wire_motion_run(screen->motion, screen, WIRE_SHAPE_DISC, now_ms);
    } else if (deck_ready(screen) && !work->flourished) {
        flourish(screen, now_ms);
    } else {
        wire_motion_end(screen->motion, screen);
    }
}

static void format_count(char *text, size_t capacity, uint32_t value)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append_unsigned(&builder, value);
}

static void format_days(char *text, size_t capacity, uint16_t days)
{
    StringBuilder builder = string_builder_make(text, capacity);
    if (days == 1) {
        string_builder_append(&builder, i18n_text(STR_RECAP_ONE_DAY));
    } else {
        i18n_format_counts(&builder, STR_RECAP_DAYS, days, 0);
    }
}

static void format_hour(char *text, size_t capacity, uint8_t hour)
{
    StringBuilder builder = string_builder_make(text, capacity);
    char number[4];
    StringBuilder digits = string_builder_make(number, sizeof number);
    bool full_day = settings == NULL || wall_time_24h(settings);
    uint8_t shown =
        full_day ? hour
                 : (uint8_t)(hour % HALF_DAY_HOURS == 0 ? HALF_DAY_HOURS : hour % HALF_DAY_HOURS);
    if (full_day && hour < TEN) {
        string_builder_append_char(&digits, '0');
    }
    string_builder_append_unsigned(&digits, shown);
    if (full_day) {
        i18n_format_text(&builder, STR_RECAP_HOUR_24, number);
        return;
    }
    const char *arguments[] = {number,
                               i18n_text(hour < HALF_DAY_HOURS ? STR_CLOCK_AM : STR_CLOCK_PM)};
    i18n_format(&builder, STR_CLOCK_12_HOUR, arguments, 2);
}

static void format_period(char *text, size_t capacity, uint8_t period)
{
    StringBuilder builder = string_builder_make(text, capacity);
    if (period == LISTENING_ALL_TIME) {
        string_builder_append(&builder, i18n_text(STR_RECAP_EVERY_PLAY));
        return;
    }
    CalendarDate date = calendar_date(recap()->span.first_day);
    char year[8];
    format_count(year, sizeof year, date.year);
    if (period == LISTENING_THIS_YEAR) {
        string_builder_append(&builder, year);
        return;
    }
    const char *arguments[] = {i18n_text((StringId)(STR_JANUARY + date.month - 1)), year};
    i18n_format(&builder, STR_CALENDAR_MONTH, arguments, 2);
}

static int line_of(const Font *font)
{
    return font_line_height(font);
}

static Rect inset(Rect area, int by)
{
    return (Rect){area.x + by, area.y + by, area.width - 2 * by, area.height - 2 * by};
}

static void draw_aligned(Pane pane, TextStyle style, Rect area, const char *text, int align)
{
    int width = text_width(style.font, text);
    width = width < area.width ? width : area.width;
    area.x += (area.width - width) * align / 2;
    area.width = align == 0 ? area.width : width;
    label_draw(pane.canvas, style, (Label){area, text});
}

static void draw_text(Pane pane, TextStyle style, Rect area, const char *text)
{
    draw_aligned(pane, style, area, text, 0);
}

static const Font *narrowest(const Theme *theme, const Font *font, const char *text, int width)
{
    if (text_width(font, text) <= width) {
        return font;
    }
    return text_width(theme->fonts.body, text) <= width ? theme->fonts.body : theme->fonts.small;
}

static void draw_page_bar(Pane pane, uint8_t place, uint8_t count)
{
    const ThemeColors *colors = &pane.theme->colors;
    int padding = pane.theme->metrics.padding;
    int span = pane.area.width - 2 * padding + BAR;
    for (int index = 0; index < count; index++) {
        int left = pane.area.x + padding + index * span / count;
        int right = pane.area.x + padding + (index + 1) * span / count - BAR;
        canvas_fill_rect(pane.canvas, (Rect){left, pane.area.y + BAR, right - left, BAR},
                         index <= place ? colors->accent : colors->divider);
    }
}

static void draw_figure(ListeningScreen *screen, Pane pane, Rect space)
{
    WireMotion *motion = screen->motion;
    if (motion->owner == screen && motion->running) {
        wire_motion_draw(motion, pane.canvas, pane.theme, space);
    } else if (work->flourished) {
        wire_motion_draw(&work->still, pane.canvas, pane.theme, space);
    }
}

static void draw_title_words(ListeningScreen *screen, Pane pane, Rect area)
{
    const Theme *theme = pane.theme;
    char period[VALUE_BYTES];
    format_period(period, sizeof period, screen->period);
    int title = line_of(theme->fonts.title);
    int body = line_of(theme->fonts.body);
    int top = area.y + (area.height - title - body - GAP) / 2;
    Rect heading = {area.x, top, area.width, title};
    Rect detail = {area.x, top + title + GAP, area.width, body};
    const char *name = i18n_text(PERIOD_TITLES[screen->period]);
    TextStyle strong = {narrowest(theme, theme->fonts.title, name, area.width), theme->colors.text};
    TextStyle accent = {narrowest(theme, theme->fonts.body, period, area.width),
                        theme->colors.accent};
    int align = theme_is_portrait(theme) ? 1 : 0;
    draw_aligned(pane, strong, heading, name, align);
    draw_aligned(pane, accent, detail, period, align);
    canvas_fill_rect(pane.canvas,
                     (Rect){detail.x, heading.y + title + GAP / 2 - 1,
                            theme_is_portrait(theme) ? detail.width : area.width / 3, 1},
                     theme->colors.accent_alt);
}

static void draw_title_card(ListeningScreen *screen, Pane pane)
{
    const Theme *theme = pane.theme;
    int hint = line_of(theme->fonts.small);
    Rect inner = inset(pane.area, theme->metrics.padding);
    inner.height -= hint;
    bool portrait = theme_is_portrait(theme);
    Rect figure = portrait ? (Rect){inner.x, inner.y, inner.width, inner.height / 2}
                           : (Rect){inner.x, inner.y, inner.width * 2 / 5, inner.height};
    Rect words = portrait ? (Rect){inner.x, figure.y + figure.height, inner.width,
                                   inner.height - figure.height}
                          : (Rect){figure.x + figure.width + GAP, inner.y,
                                   inner.width - figure.width - GAP, inner.height};
    draw_figure(screen, pane, figure);
    draw_title_words(screen, pane, words);
    draw_aligned(pane, (TextStyle){theme->fonts.small, theme->colors.text_secondary},
                 (Rect){inner.x, inner.y + inner.height, inner.width, hint},
                 i18n_text(STR_RECAP_HINT), 1);
}

static Color tile_color(const Theme *theme)
{
    return color_between(theme->colors.background, theme->colors.divider, 1, 2);
}

static void draw_tile(Pane pane, Rect area, Tile tile)
{
    const Theme *theme = pane.theme;
    canvas_fill_rect(pane.canvas, area, tile_color(theme));
    canvas_fill_rect(pane.canvas, (Rect){area.x, area.y, RULE, area.height}, theme->colors.accent);
    int small = line_of(theme->fonts.small);
    const Font *font = theme->fonts.title;
    int top = area.y + (area.height - small - line_of(font)) / 2;
    Rect text = {area.x + GAP + RULE, top, area.width - 2 * GAP - RULE, small};
    draw_text(pane, (TextStyle){theme->fonts.small, theme->colors.text_secondary}, text,
              i18n_text(tile.label));
    text.y += small;
    text.height = line_of(font);
    draw_text(pane, (TextStyle){font, theme->colors.text}, text, tile.value);
}

static void draw_tiles(Pane pane, const Tile *tiles, int count, int columns)
{
    int rows = (count + columns - 1) / columns;
    int width = (pane.area.width - (columns - 1) * GAP) / columns;
    int height = (pane.area.height - (rows - 1) * GAP) / rows;
    for (int index = 0; index < count; index++) {
        Rect area = {pane.area.x + index % columns * (width + GAP),
                     pane.area.y + index / columns * (height + GAP), width, height};
        draw_tile(pane, area, tiles[index]);
    }
}

static void draw_time_card(Pane pane)
{
    const Theme *theme = pane.theme;
    const ListeningRecap *found = recap();
    char values[SHARES + 1][VALUE_BYTES];
    listening_screen_format_time(values[0], VALUE_BYTES, found->totals.listened_ms);
    format_count(values[1], VALUE_BYTES, found->totals.plays);
    format_count(values[2], VALUE_BYTES, found->totals.skips);
    format_count(values[3], VALUE_BYTES, found->artists);
    bool portrait = theme_is_portrait(theme);
    int title = line_of(theme->fonts.title);
    Rect hero = {pane.area.x, pane.area.y, pane.area.width, title + 2 * GAP};
    draw_aligned(pane, (TextStyle){theme->fonts.title, theme->colors.accent}, hero, values[0], 1);
    Tile tiles[] = {{STR_LISTENING_PLAYS, values[1]},
                    {STR_LISTENING_SKIPS, values[2]},
                    {STR_ARTISTS, values[3]}};
    Pane rest = {pane.canvas, theme,
                 (Rect){pane.area.x, hero.y + hero.height + GAP, pane.area.width,
                        pane.area.height - hero.height - GAP}};
    draw_tiles(rest, tiles, SHARES, portrait ? 1 : SHARES);
}

static void draw_cover(Pane pane, Rect area, uint8_t index)
{
    const Theme *theme = pane.theme;
    uint16_t album = work->albums[index];
    Image image;
    bool loaded = covers != NULL && album != NO_ALBUM &&
                  cover_cache_half(covers, COVER_POSITION + index, album, &image) == COVER_LOADED;
    if (loaded) {
        image_draw_quad(pane.canvas, image,
                        (Quad){area.x, area.width, area.y, area.height, area.y, area.height});
        return;
    }
    canvas_fill_rect(pane.canvas, area, tile_color(theme));
    icon_draw(pane.canvas, ICON_ALBUM, inset(area, area.width / 4),
              (IconInk){theme->colors.text_secondary, theme->colors.accent});
}

static int draw_rank_number(Pane pane, Rect area, uint8_t index)
{
    const Theme *theme = pane.theme;
    char number[4];
    format_count(number, sizeof number, index + 1u);
    int column = text_width(theme->fonts.title, "5") + GAP;
    draw_text(pane, (TextStyle){theme->fonts.title, theme->colors.accent_alt},
              (Rect){area.x, area.y, column, area.height}, number);
    return column;
}

static void draw_rank_words(Pane pane, Rect area, RankRow row, const char *time)
{
    const Theme *theme = pane.theme;
    const char *artist = row.group > 0 ? work->artists[row.group - 1][row.index] : NULL;
    TextStyle name = {theme->fonts.body, theme->colors.text};
    TextStyle quiet = {theme->fonts.small, theme->colors.text_secondary};
    int body = line_of(theme->fonts.body);
    int small = line_of(theme->fonts.small);
    if (!row.stacked) {
        int width = text_width(theme->fonts.small, time) + GAP;
        draw_text(pane, name, (Rect){area.x, area.y, area.width - width, area.height},
                  work->names[row.group][row.index]);
        draw_aligned(pane, quiet, area, time, 2);
        return;
    }
    int lines = artist != NULL ? 2 : 1;
    Rect line = {area.x, area.y + (area.height - body - lines * small) / 2, area.width, body};
    draw_text(pane, name, line, work->names[row.group][row.index]);
    line = (Rect){line.x, line.y + body, line.width, small};
    if (artist != NULL) {
        draw_text(pane, quiet, line, artist);
        line.y += small;
    }
    draw_text(pane, (TextStyle){theme->fonts.small, theme->colors.accent}, line, time);
}

static void draw_rank_row(Pane pane, RankRow row)
{
    const Theme *theme = pane.theme;
    char time[VALUE_BYTES];
    listening_screen_format_time(time, sizeof time, recap()->top[row.group][row.index].listened_ms);
    Rect area = {row.area.x + theme->metrics.padding, row.area.y,
                 row.area.width - 2 * theme->metrics.padding, row.area.height};
    int used = draw_rank_number(pane, area, row.index);
    if (row.group == ALBUM_GROUP) {
        int side = area.height - GAP / 2;
        side = side < COVER_HALF_SIZE ? side : COVER_HALF_SIZE;
        side = side < area.width / 4 ? side : area.width / 4;
        draw_cover(pane, (Rect){area.x + used, area.y + (area.height - side) / 2, side, side},
                   row.index);
        used += side + GAP;
    }
    draw_rank_words(pane, (Rect){area.x + used, area.y, area.width - used, area.height}, row, time);
}

static void draw_ranked(Pane pane, uint8_t group)
{
    const Theme *theme = pane.theme;
    int height = pane.area.height / LISTENING_RECAP_TOP;
    bool stacked = height >= line_of(theme->fonts.body) + 2 * line_of(theme->fonts.small) + GAP / 2;
    for (uint8_t index = 0; index < recap()->counts[group]; index++) {
        Rect area = {pane.area.x, pane.area.y + index * height, pane.area.width, height};
        draw_rank_row(pane, (RankRow){area, group, index, stacked});
        if (index + 1 < recap()->counts[group]) {
            canvas_fill_rect(pane.canvas,
                             (Rect){area.x + theme->metrics.padding, area.y + height - 1,
                                    area.width - 2 * theme->metrics.padding, 1},
                             theme->colors.divider);
        }
    }
}

static int add_tile(Tile *tiles, int count, Tile tile)
{
    tiles[count] = tile;
    return count + 1;
}

static void draw_rhythm_card(Pane pane)
{
    const ListeningRecap *found = recap();
    char values[TILE_LIMIT][VALUE_BYTES];
    Tile tiles[TILE_LIMIT];
    int count = 0;
    if (found->weekday != LISTENING_RECAP_NONE) {
        count = add_tile(tiles, count,
                         (Tile){STR_RECAP_DAY, i18n_text((StringId)(STR_MONDAY + found->weekday))});
        format_hour(values[1], VALUE_BYTES, found->hour);
        count = add_tile(tiles, count, (Tile){STR_RECAP_HOUR, values[1]});
        format_days(values[2], VALUE_BYTES, found->streak_days);
        count = add_tile(tiles, count, (Tile){STR_RECAP_STREAK, values[2]});
    }
    format_count(values[3], VALUE_BYTES, found->span.dated ? found->new_artists : found->artists);
    count = add_tile(tiles, count,
                     (Tile){found->span.dated ? STR_RECAP_NEW_ARTISTS : STR_ARTISTS, values[3]});
    draw_tiles(pane, tiles, count, theme_is_portrait(pane.theme) || count == 1 ? 1 : 2);
}

static void draw_share(Pane pane, Rect area, StringId label, uint32_t percent)
{
    const Theme *theme = pane.theme;
    char value[VALUE_BYTES];
    StringBuilder builder = string_builder_make(value, sizeof value);
    i18n_format_counts(&builder, STR_RECAP_PERCENT, percent, 0);
    int small = line_of(theme->fonts.small);
    int top = area.y + (area.height - small - GAP / 2 - BAR) / 2;
    Rect words = {area.x, top, area.width, small};
    draw_text(pane, (TextStyle){theme->fonts.small, theme->colors.text}, words, i18n_text(label));
    draw_aligned(pane, (TextStyle){theme->fonts.small, theme->colors.accent}, words, value, 2);
    Rect track = {area.x, top + small + GAP / 2, area.width, BAR};
    canvas_fill_rect(pane.canvas, track, theme->colors.divider);
    canvas_fill_rect(pane.canvas,
                     (Rect){track.x, track.y, track.width * (int)percent / PERCENT, BAR},
                     theme->colors.accent);
}

static void draw_shares(Pane pane, const uint64_t *parts, const StringId *labels)
{
    uint64_t whole = parts[0] + parts[1] + parts[2];
    int height = pane.area.height / SHARES;
    for (int index = 0; index < SHARES; index++) {
        uint32_t percent =
            whole == 0 ? 0 : (uint32_t)((parts[index] * PERCENT + whole / 2) / whole);
        draw_share(pane, (Rect){pane.area.x, pane.area.y + index * height, pane.area.width, height},
                   labels[index], percent);
    }
}

static void draw_sound_card(Pane pane)
{
    static const StringId OUTPUTS[SHARES] = {STR_RECAP_WIRED, STR_BLUETOOTH, STR_USB_DAC_TITLE};
    static const StringId FORMATS[SHARES] = {STR_RECAP_LOSSY, STR_RECAP_LOSSLESS, STR_RECAP_HIRES};
    const ListeningRecap *found = recap();
    uint64_t outputs[SHARES] = {found->output_ms[LISTENING_OUTPUT_SINGLE_ENDED] +
                                    found->output_ms[LISTENING_OUTPUT_BALANCED],
                                found->output_ms[LISTENING_OUTPUT_BLUETOOTH],
                                found->output_ms[LISTENING_OUTPUT_USB_DAC]};
    Rect area = inset(pane.area, pane.theme->metrics.padding);
    bool portrait = theme_is_portrait(pane.theme);
    Rect first = portrait ? (Rect){area.x, area.y, area.width, (area.height - GAP) / 2}
                          : (Rect){area.x, area.y, (area.width - 2 * GAP) / 2, area.height};
    Rect second = portrait
                      ? (Rect){area.x, first.y + first.height + GAP, area.width, first.height}
                      : (Rect){first.x + first.width + 2 * GAP, area.y, first.width, area.height};
    draw_shares((Pane){pane.canvas, pane.theme, first}, outputs, OUTPUTS);
    draw_shares((Pane){pane.canvas, pane.theme, second}, found->format_ms, FORMATS);
}

static void draw_closing_card(Pane pane)
{
    const ListeningRecap *found = recap();
    char time[VALUE_BYTES];
    char streak[VALUE_BYTES];
    Tile tiles[TILE_LIMIT];
    int count = 0;
    if (found->counts[0] > 0) {
        count = add_tile(tiles, count, (Tile){STR_RECAP_TOP_ARTIST, work->names[0][0]});
    }
    if (found->counts[2] > 0) {
        count = add_tile(tiles, count, (Tile){STR_RECAP_TOP_SONG, work->names[2][0]});
    }
    listening_screen_format_time(time, sizeof time, found->totals.listened_ms);
    count = add_tile(tiles, count, (Tile){STR_TOTAL_LISTENING, time});
    if (found->streak_days > 0) {
        format_days(streak, sizeof streak, found->streak_days);
        count = add_tile(tiles, count, (Tile){STR_RECAP_STREAK, streak});
    }
    draw_tiles(pane, tiles, count, theme_is_portrait(pane.theme) || count == 1 ? 1 : 2);
}

static void draw_notice(Pane pane, StringId headline, StringId detail)
{
    listening_screen_draw_status(
        pane.theme, pane.canvas, pane.area,
        (ExportStatus){i18n_text(headline), i18n_text(detail), NULL, NULL});
}

static void draw_card(ListeningScreen *screen, Pane pane, uint8_t card)
{
    Rect body = {pane.area.x, pane.area.y + GAP + BAR, pane.area.width,
                 pane.area.height - GAP - BAR};
    Pane inner = {pane.canvas, pane.theme, body};
    Pane padded = {pane.canvas, pane.theme, inset(body, pane.theme->metrics.padding)};
    padded.area.y -= pane.theme->metrics.padding / 2;
    padded.area.height += pane.theme->metrics.padding / 2;
    if (card == CARD_TITLE) {
        home_art_draw_backdrop(pane.canvas, pane.area, pane.theme);
        draw_title_card(screen, inner);
    } else if (card == CARD_TIME) {
        draw_time_card(padded);
    } else if (card >= CARD_ARTISTS && card <= CARD_SONGS) {
        draw_ranked(inner, (uint8_t)(card - CARD_ARTISTS));
    } else if (card == CARD_RHYTHM) {
        draw_rhythm_card(padded);
    } else if (card == CARD_SOUND) {
        draw_sound_card(inner);
    } else {
        draw_closing_card(padded);
    }
    draw_page_bar(pane, screen->card, recap_cards_count(screen));
}

void recap_cards_draw(ListeningScreen *screen, Canvas *canvas, Rect content)
{
    const Theme *theme = screen->theme;
    Pane pane = {canvas, theme, content};
    bool mine = listening_module_work.owner == screen;
    uint8_t stage = mine ? work->stage : STAGE_WORKING;
    if (stage == STAGE_NO_CLOCK) {
        draw_notice(pane, STR_TIME_UNKNOWN, STR_TIME_UNKNOWN_HINT);
    } else if (stage == STAGE_FAILED) {
        message_draw(canvas, theme, content, i18n_text(STR_HISTORY_UNAVAILABLE));
    } else if (stage < STAGE_READY) {
        MessageConfig working = {
            .message = i18n_text(STR_LISTENING_WORKING), .theme = theme, .motion = screen->motion};
        message_draw_working(canvas, content, &working);
    } else if (recap()->totals.plays == 0) {
        draw_notice(pane,
                    screen->period == LISTENING_ALL_TIME ? STR_LISTENING_EMPTY : STR_RECAP_EMPTY,
                    STR_LISTENING_HINT);
    } else {
        draw_card(screen, pane, current_card(screen));
    }
}
