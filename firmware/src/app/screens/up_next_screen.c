#include "up_next_screen.h"

#include <stdbool.h>

#include "core/i18n.h"
#include "neon_art.h"
#include "ui/footer_hints.h"
#include "ui/header.h"
#include "ui/icons.h"
#include "ui/label.h"
#include "ui/list_navigation.h"

static const StringId EDIT_LABELS[UP_NEXT_EDIT_COUNT] = {
    STR_PLAY_NEXT, STR_MOVE_EARLIER, STR_MOVE_LATER, STR_REMOVE, STR_CLEAR_ADDED,
};

typedef enum RowKind {
    ROW_PLAYING,
    ROW_ADDED,
    ROW_QUEUE,
    ROW_ACTION
} RowKind;

enum {
    FOCUS_BAR = 3,
    TRACK_GAP = 2,
    TRACK_LIFT = 3,
    EMPTY_COLUMN = 124,
    QUEUE_WIDTH = 72,
    QUEUE_HEIGHT = 46,
    QUEUE_INSET = 8,
    QUEUE_PITCH = 9,
    QUEUE_LINES = 3,
    QUEUE_PLUS = 5,
    ACTION_LINES = 3
};

typedef struct QueueWalk {
    PlayQueueCursor cursor;
    int steps;
} QueueWalk;

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static RowKind kind_of(const UpNextScreen *up_next, int index)
{
    if (index == 0) {
        return ROW_PLAYING;
    }
    if (index >= up_next->count) {
        return ROW_ACTION;
    }
    return index <= up_next->added ? ROW_ADDED : ROW_QUEUE;
}

static bool wraps(const Player *player)
{
    return player->repeat == REPEAT_ALL;
}

static bool walk_to(const Player *player, QueueWalk *walk, int steps)
{
    if (player->queue.stored_order != NULL) {
        uint32_t position = (uint32_t)player->queue.cursor.position + (uint32_t)steps;
        if (player->queue.count == 0 || (!wraps(player) && position >= player->queue.count)) {
            return false;
        }
        walk->cursor = (PlayQueueCursor){.position = (uint16_t)(position % player->queue.count)};
        walk->steps = steps;
        return true;
    }
    while (walk->steps < steps) {
        PlayQueueCursor next;
        if (!play_queue_step(&player->queue, walk->cursor, 1, wraps(player), &next)) {
            return false;
        }
        walk->cursor = next;
        walk->steps++;
    }
    return true;
}

static QueueWalk start_walk(const Player *player)
{
    return (QueueWalk){.cursor = player->queue.cursor, .steps = 0};
}

static int count_queue_rows(const Player *player)
{
    if (player->queue.stored_order != NULL) {
        return wraps(player) ? player->queue.count - 1
                             : player->queue.count - player->queue.cursor.position - 1;
    }
    QueueWalk walk = start_walk(player);
    PlayQueueCursor start = walk.cursor;
    int rows = 0;
    while (rows < UP_NEXT_QUEUE_ROWS && walk_to(player, &walk, rows + 1)) {
        if (walk.cursor.position == start.position &&
            walk.cursor.album_track == start.album_track) {
            break;
        }
        rows++;
    }
    return rows;
}

typedef struct UpNextRows {
    const UpNextScreen *owner;
    int first;
    int count;
    char labels[UP_NEXT_ROWS][UP_NEXT_LABEL_CAPACITY];
    char details[UP_NEXT_ROWS][UP_NEXT_DETAIL_CAPACITY];
} UpNextRows;

static UpNextRows cache;

static void count_rows(UpNextScreen *up_next)
{
    const Player *player = up_next->config.player;
    up_next->added = player->up_next.count;
    up_next->count = player->has_track ? 1 + up_next->added + count_queue_rows(player) : 0;
    cache.owner = NULL;
}

static bool track_at(const UpNextScreen *up_next, int index, QueueWalk *walk, uint16_t *track)
{
    const Player *player = up_next->config.player;
    switch (kind_of(up_next, index)) {
    case ROW_PLAYING:
        *track = player->track;
        return true;
    case ROW_ADDED:
        *track = player->up_next.tracks[index - 1];
        return true;
    case ROW_QUEUE:
    case ROW_ACTION:
        break;
    }
    return walk_to(player, walk, index - up_next->added) &&
           play_queue_track_at(&player->queue, walk->cursor, track);
}

static const char *state_text(const Player *player)
{
    return player->state == PLAYER_PLAYING ? i18n_text(STR_PLAYING) : i18n_text(STR_PAUSED);
}

static void load_row(UpNextScreen *up_next, int row, int index, QueueWalk *walk)
{
    const Library *library = up_next->config.library;
    char *label = cache.labels[row];
    char *detail = cache.details[row];
    label[0] = '\0';
    detail[0] = '\0';
    uint16_t track_index = 0;
    LibraryTrack track;
    if (!track_at(up_next, index, walk, &track_index) ||
        !library_read_track(library, track_index, &track)) {
        return;
    }
    library_read_string(library, track.title, label, UP_NEXT_LABEL_CAPACITY);
    LibraryGroup artist;
    if (library_read_group(library, LIBRARY_ARTISTS, track.artist, &artist)) {
        library_read_string(library, artist.name, detail, UP_NEXT_DETAIL_CAPACITY);
    }
}

static void refresh_rows(UpNextScreen *up_next)
{
    if (cache.owner == up_next && cache.first == up_next->list.top) {
        return;
    }
    int top = up_next->list.top;
    int count = min_of(min_of(up_next->list.visible_rows, UP_NEXT_ROWS), up_next->count - top);
    QueueWalk walk = start_walk(up_next->config.player);
    for (int row = 0; row < count; row++) {
        load_row(up_next, row, top + row, &walk);
    }
    cache.owner = up_next;
    cache.first = top;
    cache.count = count;
}

static const char *cached_text(const UpNextScreen *up_next, int index, bool detail)
{
    int row = index - cache.first;
    if (cache.owner != up_next || row < 0 || row >= cache.count) {
        return "";
    }
    return detail ? cache.details[row] : cache.labels[row];
}

static bool only_playing(const UpNextScreen *up_next)
{
    return up_next->count == 1;
}

static int row_count(const void *context)
{
    const UpNextScreen *up_next = context;
    return up_next->count + (only_playing(up_next) ? 1 : 0);
}

static const char *row_label(const void *context, int index)
{
    const UpNextScreen *up_next = context;
    return kind_of(up_next, index) == ROW_ACTION ? i18n_text(STR_ADD_TRACKS_FROM_MUSIC)
                                                 : cached_text(context, index, false);
}

static bool row_opens(const void *context, int index)
{
    return kind_of(context, index) == ROW_ACTION;
}

const char *up_next_screen_badge(const UpNextScreen *up_next, int index)
{
    if (index == 0) {
        return state_text(up_next->config.player);
    }
    return index == 1 && index < up_next->count ? i18n_text(STR_UP_NEXT_BADGE) : NULL;
}

static ScreenAction select_row(void *context, int index)
{
    UpNextScreen *up_next = context;
    Player *player = up_next->config.player;
    QueueWalk walk = start_walk(player);
    switch (kind_of(up_next, index)) {
    case ROW_PLAYING:
        break;
    case ROW_ADDED:
        player_jump_to_up_next(player, (uint16_t)(index - 1));
        break;
    case ROW_QUEUE:
        if (walk_to(player, &walk, index - up_next->added)) {
            player_jump_to_queue(player, walk.cursor);
        }
        break;
    case ROW_ACTION:
        return up_next->config.music != NULL ? screen_action_push(up_next->config.music)
                                             : screen_action_none();
    }
    return up_next->config.now_playing != NULL ? screen_action_push(up_next->config.now_playing)
                                               : screen_action_pop();
}

static bool action_shown(const void *context)
{
    const UpNextEntry *entry = context;
    const UpNextScreen *up_next = entry->owner;
    int index = up_next->edit_index;
    if (entry->snapshot) {
        return index == -1 && up_next->config.player->queue.stored_order == NULL;
    }
    switch (entry->edit) {
    case UP_NEXT_MOVE_FIRST:
    case UP_NEXT_MOVE_EARLIER:
        return index > 0;
    case UP_NEXT_MOVE_LATER:
        return index >= 0 && index + 1 < up_next->edit_count;
    case UP_NEXT_REMOVE:
        return index >= 0;
    case UP_NEXT_CLEAR:
        return up_next->edit_full || up_next->edit_count > 0;
    case UP_NEXT_EDIT_COUNT:
        return false;
    }
    return false;
}

static void show_message(UpNextScreen *up_next, const char *message)
{
    if (up_next->config.toast != NULL) {
        toast_show(up_next->config.toast, message, up_next->actions.now_ms);
    }
}

static void select_after_edit(UpNextScreen *up_next, UpNextEdit edit)
{
    int selected = up_next->edit_index + 1;
    switch (edit) {
    case UP_NEXT_MOVE_FIRST:
        selected = 1;
        break;
    case UP_NEXT_MOVE_EARLIER:
        selected--;
        break;
    case UP_NEXT_MOVE_LATER:
        selected++;
        break;
    case UP_NEXT_CLEAR:
        selected = 0;
        break;
    case UP_NEXT_REMOVE:
    case UP_NEXT_EDIT_COUNT:
        break;
    }
    up_next->list.selected = selected;
}

static ScreenAction edit_queue(void *context)
{
    UpNextEntry *entry = context;
    UpNextScreen *up_next = entry->owner;
    Player *player = up_next->config.player;
    PlayerQueueEditResult result;
    if (entry->snapshot) {
        result = player->up_next_revision == up_next->edit_revision ? player_snapshot_queue(player)
                                                                    : PLAYER_QUEUE_EDIT_STALE;
    } else if (up_next->edit_full) {
        result = player_edit_queue(player, up_next->edit_revision,
                                   (uint16_t)(up_next->edit_index + 1), entry->edit);
    } else {
        result = player_edit_up_next(player, up_next->edit_revision, (uint16_t)up_next->edit_index,
                                     entry->edit);
    }
    if (result == PLAYER_QUEUE_EDIT_SAVE_FAILED) {
        show_message(up_next, i18n_text(STR_QUEUE_SAVE_FAILED));
        return screen_action_none();
    }
    if (result != PLAYER_QUEUE_EDITED && result != PLAYER_QUEUE_EDIT_WORKING) {
        show_message(up_next, i18n_text(STR_QUEUE_CHANGED));
    } else {
        select_after_edit(up_next, entry->edit);
    }
    return screen_action_pop();
}

static ScreenAction open_actions(void *context, int index)
{
    UpNextScreen *up_next = context;
    if (index < 0 || index >= up_next->count || player_queue_busy(up_next->config.player)) {
        return screen_action_none();
    }
    up_next->edit_index = index - 1;
    up_next->edit_full = up_next->config.player->queue.stored_order != NULL ||
                         index > up_next->added || up_next->added == 0;
    up_next->edit_count = up_next->edit_full ? up_next->count - 1 : up_next->added;
    up_next->items[UP_NEXT_CLEAR].label =
        up_next->edit_full ? i18n_text(STR_KEEP_CURRENT) : i18n_text(STR_CLEAR_ADDED);
    up_next->edit_revision = up_next->config.player->up_next_revision;
    up_next->returning = true;
    MenuConfig config = {.title = i18n_text(STR_EDIT_UP_NEXT),
                         .items = up_next->items,
                         .item_count = UP_NEXT_ACTION_COUNT,
                         .theme = up_next->config.theme};
    return screen_action_push(menu_screen_init(&up_next->actions, config));
}

static int track_row(const UpNextScreen *up_next, int selected)
{
    return selected < up_next->count || up_next->count == 0 ? selected : up_next->count - 1;
}

static void observe_player(UpNextScreen *up_next)
{
    const Player *player = up_next->config.player;
    up_next->seen_revision = player->up_next_revision;
    up_next->seen_token = player->current_token;
    up_next->seen_state = player->state;
    up_next->seen_repeat = player->repeat;
    up_next->seen_shuffle = player->shuffle;
    up_next->seen_has_track = player->has_track;
}

static void refresh_player(UpNextScreen *up_next)
{
    const Player *player = up_next->config.player;
    if (up_next->seen_revision == player->up_next_revision &&
        up_next->seen_token == player->current_token && up_next->seen_state == player->state &&
        up_next->seen_repeat == player->repeat && up_next->seen_shuffle == player->shuffle &&
        up_next->seen_has_track == player->has_track) {
        return;
    }
    count_rows(up_next);
    list_view_select(&up_next->list, track_row(up_next, up_next->list.selected), up_next->now_ms);
    observe_player(up_next);
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    UpNextScreen *up_next = context;
    up_next->now_ms = now_ms;
    refresh_player(up_next);
    ListSelection selection = {.select = select_row, .hold = open_actions, .context = up_next};
    ScreenAction action = list_navigation_handle(&up_next->list, event, now_ms, selection);
    refresh_rows(up_next);
    return action;
}

static const FooterHint UP_NEXT_HINTS[] = {{FOOTER_KEY_PLAY, STR_HINT_OPEN},
                                           {FOOTER_KEY_HOLD_PLAY, STR_EDIT_UP_NEXT}};
static const FooterHints UP_NEXT_FOOTER = {
    .items = UP_NEXT_HINTS, .count = 2, .align = FOOTER_ALIGN_START};

static Rect rows_area(const Theme *theme)
{
    Rect content = theme_content_area(theme);
    content.height -= footer_hints_height(theme, UP_NEXT_FOOTER);
    return content;
}

static int track_height(const Theme *theme)
{
    return theme->metrics.row_height + font_line_height(theme->fonts.small) + TRACK_GAP;
}

static void fit_rows(UpNextScreen *up_next)
{
    const Theme *theme = up_next->config.theme;
    int rows = rows_area(theme).height / track_height(theme);
    list_view_fit_rows(&up_next->list, rows < UP_NEXT_ROWS ? rows : UP_NEXT_ROWS);
}

static void enter(void *context, uint32_t now_ms)
{
    UpNextScreen *up_next = context;
    up_next->now_ms = now_ms;
    int selected = up_next->returning ? up_next->list.selected : 0;
    up_next->returning = false;
    count_rows(up_next);
    observe_player(up_next);
    list_view_init(&up_next->list, &up_next->model, up_next->config.theme, now_ms);
    fit_rows(up_next);
    list_view_select(&up_next->list, track_row(up_next, selected), now_ms);
    refresh_rows(up_next);
}

static void update(void *context, uint32_t now_ms)
{
    UpNextScreen *up_next = context;
    up_next->now_ms = now_ms;
    refresh_player(up_next);
    refresh_rows(up_next);
}

const char *up_next_screen_artist(const UpNextScreen *up_next, int index)
{
    return cached_text(up_next, index, true);
}

static void draw_track(const UpNextScreen *up_next, Canvas *canvas, int index, Rect row)
{
    const Theme *theme = up_next->config.theme;
    TextStyle small = {theme->fonts.small, theme->colors.text_secondary, false};
    int padding = theme->metrics.padding;
    int line = font_line_height(small.font);
    if (index == up_next->list.selected) {
        canvas_fill_rect(canvas, row, theme_selection_fill(theme));
        canvas_fill_rect(canvas, (Rect){row.x, row.y, FOCUS_BAR, row.height}, theme->colors.accent);
    }
    const char *badge = up_next_screen_badge(up_next, index);
    int room = badge != NULL ? text_width(small.font, badge) + theme->metrics.detail_gap : 0;
    bool under = theme_is_portrait(theme);
    Rect title = {row.x, row.y, row.width - (under ? 0 : room), theme->metrics.row_height};
    list_view_draw_row(&up_next->list, canvas, index, title, up_next->now_ms);
    Rect artist = {row.x + padding, row.y + title.height - TRACK_LIFT,
                   row.width - 2 * padding - (under ? room : 0), line};
    if (badge != NULL) {
        Rect area = {row.x + row.width - padding - room + theme->metrics.detail_gap,
                     under ? artist.y : row.y + (title.height - line) / 2, room, line};
        label_draw(canvas, small, (Label){area, badge});
    }
    label_draw(canvas, small, (Label){artist, up_next_screen_artist(up_next, index)});
}

static void draw_queue(Canvas *canvas, const Theme *theme, Rect area)
{
    static const uint8_t LENGTHS[QUEUE_LINES] = {70, 48, 58};
    Rect box = {area.x + (area.width - QUEUE_WIDTH) / 2, area.y + (area.height - QUEUE_HEIGHT) / 2,
                QUEUE_WIDTH, QUEUE_HEIGHT};
    neon_art_frame(canvas, box, neon_art_ink(theme, theme->colors.accent));
    int inner = box.width - 2 * QUEUE_INSET;
    for (int line = 0; line < QUEUE_LINES; line++) {
        Rect rule = {box.x + QUEUE_INSET, box.y + QUEUE_INSET + 2 + line * QUEUE_PITCH,
                     inner * LENGTHS[line] / 100, 1};
        canvas_fill_rect(canvas, rule,
                         line == 0 ? theme->colors.accent : theme->colors.text_secondary);
    }
    Point plus = {box.x + box.width - QUEUE_INSET - QUEUE_PLUS,
                  box.y + box.height - QUEUE_INSET - 2};
    canvas_fill_rect(canvas, (Rect){plus.x - QUEUE_PLUS, plus.y, 2 * QUEUE_PLUS + 1, 1},
                     theme->colors.accent);
    canvas_fill_rect(canvas, (Rect){plus.x, plus.y - QUEUE_PLUS, 1, 2 * QUEUE_PLUS + 1},
                     theme->colors.accent);
}

static int message_height(const Theme *theme, int width)
{
    const Font *font = theme->fonts.body;
    int lines = label_wrap_count(font, i18n_text(STR_UP_NEXT_EMPTY), width);
    return font_line_height(font) * (lines < 3 ? lines : 2);
}

static void draw_empty(const UpNextScreen *up_next, Canvas *canvas, Rect band)
{
    const Theme *theme = up_next->config.theme;
    int padding = theme->metrics.padding;
    Color divider = theme->colors.divider;
    canvas_fill_rect(canvas, (Rect){band.x, band.y, band.width, 1}, divider);
    canvas_fill_rect(canvas, (Rect){band.x, band.y + band.height - 1, band.width, 1}, divider);
    Rect art = {band.x, band.y, EMPTY_COLUMN, band.height};
    Rect text = {art.x + art.width + 2 * padding, band.y, band.width - art.width - 3 * padding,
                 band.height};
    if (theme_is_portrait(theme)) {
        text = (Rect){band.x + padding, 0, band.width - 2 * padding, 0};
        text.height = message_height(theme, text.width);
        art = (Rect){band.x, band.y + (band.height - QUEUE_HEIGHT - padding - text.height) / 2,
                     band.width, QUEUE_HEIGHT};
        text.y = art.y + art.height + padding;
    } else {
        canvas_fill_rect(canvas, (Rect){art.x + art.width, band.y, 1, band.height}, divider);
    }
    draw_queue(canvas, theme, art);
    int height = message_height(theme, text.width);
    label_draw_paragraph(canvas, (TextStyle){theme->fonts.body, theme->colors.text, false},
                         (Label){{text.x, text.y + (text.height - height) / 2, text.width, height},
                                 i18n_text(STR_UP_NEXT_EMPTY)});
}

static int action_height(const Theme *theme)
{
    int width =
        theme->metrics.screen_width - 3 * theme->metrics.padding - list_view_chevron_width(theme);
    const char *label = i18n_text(STR_ADD_TRACKS_FROM_MUSIC);
    int lines = label_wrap_count(theme->fonts.title, label, width);
    lines = lines < ACTION_LINES ? lines : ACTION_LINES;
    return lines > 1 ? lines * font_line_height(theme->fonts.title) + 2 * TRACK_GAP
                     : theme->metrics.row_height;
}

static void draw_action(const UpNextScreen *up_next, Canvas *canvas, Rect row)
{
    const Theme *theme = up_next->config.theme;
    bool selected = up_next->list.selected == 1;
    int padding = theme->metrics.padding;
    int chevron = list_view_chevron_width(theme);
    if (selected) {
        canvas_fill_rect(canvas, row, theme_selection_fill(theme));
        canvas_fill_rect(canvas, (Rect){row.x, row.y, FOCUS_BAR, row.height}, theme->colors.accent);
    }
    Color ink = theme->colors.chevron;
    icon_draw(canvas, ICON_CHEVRON,
              (Rect){row.x + row.width - padding - chevron, row.y, chevron, row.height},
              (IconInk){ink, ink});
    TextStyle style = {selected ? theme->fonts.title : theme->fonts.body, theme->colors.text,
                       false};
    int width = row.width - 3 * padding - chevron;
    const char *label = i18n_text(STR_ADD_TRACKS_FROM_MUSIC);
    int lines = label_wrap_count(style.font, label, width);
    lines = lines < ACTION_LINES ? lines : ACTION_LINES;
    int height = lines * font_line_height(style.font);
    label_draw_paragraph(
        canvas, style,
        (Label){{row.x + padding, row.y + (row.height - height) / 2, width, height}, label});
}

static void draw_rows(const UpNextScreen *up_next, Canvas *canvas, Rect area)
{
    const Theme *theme = up_next->config.theme;
    const ListView *list = &up_next->list;
    int height = track_height(theme);
    theme_background_draw(theme, canvas, THEME_ART_LIST, area);
    if (only_playing(up_next)) {
        int action = action_height(theme);
        draw_track(up_next, canvas, 0, (Rect){area.x, area.y, area.width, height});
        draw_empty(up_next, canvas,
                   (Rect){area.x, area.y + height, area.width, area.height - height - action});
        draw_action(up_next, canvas,
                    (Rect){area.x, area.y + area.height - action, area.width, action});
        return;
    }
    int hidden = up_next->count - list->visible_rows;
    if (hidden > 0) {
        area.width -= theme->metrics.scrollbar_width + theme->metrics.scrollbar_inset;
        list_view_draw_scrollbar(
            list, canvas,
            (Rect){area.x, area.y,
                   area.width + theme->metrics.scrollbar_width + theme->metrics.scrollbar_inset,
                   area.height},
            hidden);
    }
    for (int position = 0; position < list->visible_rows; position++) {
        int index = list->top + position;
        if (index >= up_next->count) {
            break;
        }
        draw_track(up_next, canvas, index,
                   (Rect){area.x, area.y + position * height, area.width, height});
    }
}

static void draw(void *context, Canvas *canvas)
{
    const UpNextScreen *up_next = context;
    const Theme *theme = up_next->config.theme;
    Rect content = theme_content_area(theme);
    header_draw(canvas, theme, i18n_text(STR_UP_NEXT));
    if (up_next->count == 0) {
        TextStyle style = {.font = theme->fonts.body, .color = theme->colors.text_secondary};
        canvas_fill_rect(canvas, content, theme->colors.background);
        Rect area = label_centered_area(style.font, content, i18n_text(STR_NOTHING_IS_PLAYING));
        label_draw(canvas, style, (Label){.area = area, .text = i18n_text(STR_NOTHING_IS_PLAYING)});
        return;
    }
    draw_rows(up_next, canvas, rows_area(theme));
    footer_hints_draw(canvas, theme, footer_hints_area(theme, content, UP_NEXT_FOOTER),
                      UP_NEXT_FOOTER);
}

Screen *up_next_screen_init(UpNextScreen *up_next, UpNextConfig config)
{
    up_next->config = config;
    up_next->count = 0;
    up_next->added = 0;
    cache.owner = NULL;
    up_next->now_ms = 0;
    up_next->returning = false;
    observe_player(up_next);
    for (int index = 0; index < UP_NEXT_ACTION_COUNT; index++) {
        up_next->entries[index] = (UpNextEntry){
            .owner = up_next, .edit = (UpNextEdit)index, .snapshot = index == UP_NEXT_EDIT_COUNT};
        up_next->items[index] =
            (MenuItem){.label = index == UP_NEXT_EDIT_COUNT ? i18n_text(STR_EDIT_FULL_QUEUE)
                                                            : i18n_text(EDIT_LABELS[index]),
                       .select = edit_queue,
                       .context = &up_next->entries[index],
                       .shown = action_shown,
                       .shown_context = &up_next->entries[index]};
    }
    up_next->model =
        (ListModel){.context = up_next, .count = row_count, .label = row_label, .opens = row_opens};
    list_view_init(&up_next->list, &up_next->model, config.theme, 0);
    up_next->screen = (Screen){
        .title = i18n_text(STR_UP_NEXT),
        .context = up_next,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &up_next->screen;
}
