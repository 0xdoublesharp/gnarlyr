#include "cover_flow_screen.h"

#include <stddef.h>

#include "board_layout.h"
#include "core/hash.h"
#include "core/i18n.h"
#include "core/random.h"
#include "core/string_builder.h"
#include "cover_placeholders.h"
#include "draw_profile.h"
#include "hal/clock.h"
#include "library/library_browse.h"
#include "library/sort_key.h"
#include "playing_work.h"
#include "ui/cover_flow.h"
#include "ui/label.h"
#include "ui/marquee.h"
#include "ui/message_screen.h"
#include "ui/navigation.h"

enum {
    NO_POSITION = -1,
    REFLECTION_LIGHT = 80,
    FLOOR_LEVEL = 16,
    FLOOR_GAP = 6,
    DETAIL_ALPHA = 176,
    RANDOM_POSITION = 0,
    RANDOM_SLOTS = 1,
    SHUFFLE_SIZE = PLAYING_SHUFFLE_SIZE,
    SHUFFLE_STROKE = 4
};

static const Color BACKDROP = COLOR_RGB(0, 0, 0);

static const char STAR_PREFIX[] = "\xE2\x98\x85 ";
static const Color SHUFFLE_BACKGROUND = COLOR_RGB(44, 46, 58);
static const Color SHUFFLE_MARK = COLOR_RGB(255, 255, 255);

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static int max_of(int first, int second)
{
    return first > second ? first : second;
}

CoverFlowOrder cover_flow_order_make(const Library *library)
{
    uint32_t groups = library_music_count(library, LIBRARY_ALBUM_ARTISTS);
    CoverFlowOrder order = {.first_link = 0, .count = 0, .linked = groups > 0};
    uint32_t next = 0;
    for (uint32_t index = 0; index < groups && order.linked; index++) {
        LibraryGroup group;
        if (!library_read_group(library, LIBRARY_ALBUM_ARTISTS, index, &group) ||
            (index > 0 && group.first_link != next)) {
            order.linked = false;
            break;
        }
        order.first_link = index == 0 ? group.first_link : order.first_link;
        next = group.first_link + group.link_count;
    }
    order.count =
        order.linked ? next - order.first_link : library_music_count(library, LIBRARY_ALBUMS);
    return order;
}

bool cover_flow_order_album(const Library *library, CoverFlowOrder order, uint32_t position,
                            uint16_t *album)
{
    if (position >= order.count) {
        return false;
    }
    if (!order.linked) {
        *album = (uint16_t)position;
        return true;
    }
    return library_read_index(library, LIBRARY_LINKS, order.first_link + position, album);
}

bool cover_flow_order_find(const Library *library, CoverFlowOrder order, uint16_t album,
                           uint32_t *position)
{
    LibraryAlbum record;
    LibraryGroup group;
    if (!library_read_album(library, album, &record) || album >= order.count) {
        return false;
    }
    if (!order.linked) {
        *position = album;
        return true;
    }
    if (!library_read_group(library, LIBRARY_ALBUM_ARTISTS, record.album_artist, &group)) {
        return false;
    }
    for (uint32_t link = 0; link < group.link_count; link++) {
        uint16_t value = 0;
        if (library_read_index(library, LIBRARY_LINKS, group.first_link + link, &value) &&
            value == album) {
            *position = group.first_link + link - order.first_link;
            return true;
        }
    }
    return false;
}

static bool is_artist(const CoverFlowScreen *flow)
{
    return flow->config.artist_keys != NULL;
}

static bool is_random(const CoverFlowScreen *flow, int position)
{
    return position >= RANDOM_POSITION && position < flow->first;
}

static uint16_t album_of(const CoverFlowScreen *flow, int position)
{
    uint16_t album = LIBRARY_NONE;
    if (position < flow->first ||
        !cover_flow_order_album(flow->config.library, flow->order,
                                (uint32_t)(position - flow->first), &album)) {
        return LIBRARY_NONE;
    }
    return album;
}

static void plot(int x, int y)
{
    for (int row = y - SHUFFLE_STROKE / 2; row < y + SHUFFLE_STROKE / 2; row++) {
        for (int column = x - SHUFFLE_STROKE / 2; column < x + SHUFFLE_STROKE / 2; column++) {
            if (row >= 0 && row < SHUFFLE_SIZE && column >= 0 && column < SHUFFLE_SIZE) {
                playing_module_work.as.shuffle[row * SHUFFLE_SIZE + column] = SHUFFLE_MARK;
            }
        }
    }
}

static void stroke(int from_x, int from_y, int to_x, int to_y)
{
    int steps = max_of(max_of(to_x - from_x, from_x - to_x), max_of(to_y - from_y, from_y - to_y));
    for (int step = 0; step <= steps; step++) {
        int x = from_x + (to_x - from_x) * step / max_of(steps, 1);
        int y = from_y + (to_y - from_y) * step / max_of(steps, 1);
        plot(x, y);
    }
}

static void draw_shuffle_mark(void)
{
    for (int index = 0; index < SHUFFLE_SIZE * SHUFFLE_SIZE; index++) {
        playing_module_work.as.shuffle[index] = SHUFFLE_BACKGROUND;
    }
    static const int16_t LINES[][4] = {
        {9, 34, 16, 34},  {16, 34, 30, 14}, {30, 14, 39, 14}, {39, 14, 34, 9},
        {39, 14, 34, 19}, {9, 14, 16, 14},  {16, 14, 22, 23}, {25, 27, 30, 34},
        {30, 34, 39, 34}, {39, 34, 34, 29}, {39, 34, 34, 39},
    };
    for (size_t index = 0; index < sizeof LINES / sizeof LINES[0]; index++) {
        stroke(LINES[index][0], LINES[index][1], LINES[index][2], LINES[index][3]);
    }
}

static Image shuffle_image(void)
{
    if (playing_work_claim_shuffle()) {
        draw_shuffle_mark();
    }
    return (Image){
        .pixels = playing_module_work.as.shuffle, .width = SHUFFLE_SIZE, .height = SHUFFLE_SIZE};
}

static int center_of(const CoverFlowScreen *flow)
{
    return (int)((flow->position + COVER_FLOW_ONE / 2) / COVER_FLOW_ONE);
}

static void load_year(CoverFlowScreen *flow, uint16_t year)
{
    StringBuilder builder = string_builder_make(flow->year, sizeof flow->year);
    if (year > 0) {
        string_builder_append_unsigned(&builder, year);
    }
}

static void load_text(CoverFlowScreen *flow, uint16_t album_index)
{
    const Library *library = flow->config.library;
    LibraryAlbum album;
    LibraryGroup artist;
    char title[COVER_FLOW_TEXT_CAPACITY];
    flow->title[0] = '\0';
    flow->year[0] = '\0';
    flow->artist[0] = is_artist(flow) ? flow->artist[0] : '\0';
    if (!library_read_album(library, album_index, &album) ||
        !library_read_string(library, album.title, title, sizeof title)) {
        return;
    }
    StringBuilder builder = string_builder_make(flow->title, sizeof flow->title);
    if (favorites_store_contains_album(flow->config.favorites, album_index)) {
        string_builder_append(&builder, STAR_PREFIX);
    }
    string_builder_append(&builder, title);
    if (is_artist(flow)) {
        load_year(flow, album.year);
    } else if (library_read_group(library, LIBRARY_ALBUM_ARTISTS, album.album_artist, &artist)) {
        library_read_string(library, artist.name, flow->artist, sizeof flow->artist);
    }
}

static void refresh_text(CoverFlowScreen *flow, bool force)
{
    int center = center_of(flow);
    if (flow->count == 0 || (!force && center == flow->text_position)) {
        return;
    }
    if (is_random(flow, center)) {
        StringBuilder title = string_builder_make(flow->title, sizeof flow->title);
        string_builder_append(&title, i18n_text(STR_RANDOM_ALBUM));
        StringBuilder hint = string_builder_make(flow->artist, sizeof flow->artist);
        string_builder_append(&hint, i18n_text(STR_RANDOM_ALBUM_HINT));
    } else {
        load_text(flow, album_of(flow, center));
    }
    flow->text_position = center;
    flow->text_changed_ms = flow->now_ms;
}

static bool load_half_at(CoverFlowScreen *flow, int position)
{
    return position >= flow->first && position < flow->count &&
           cover_cache_load_half(flow->config.covers, position, album_of(flow, position));
}

static bool load_nearest_half(CoverFlowScreen *flow)
{
    int center = center_of(flow);
    if (load_half_at(flow, center)) {
        return true;
    }
    int direction = flow->target < center ? -1 : 1;
    for (int distance = 1; distance <= COVER_CACHE_SLOTS / 2; distance++) {
        if (load_half_at(flow, center + direction * distance) ||
            load_half_at(flow, center - direction * distance)) {
            return true;
        }
    }
    return false;
}

static void load_covers(CoverFlowScreen *flow)
{
    if (flow->count == 0 || load_nearest_half(flow)) {
        return;
    }
    if (!is_random(flow, flow->target) &&
        flow->position == (int32_t)flow->target * COVER_FLOW_ONE) {
        cover_cache_load_full(flow->config.covers, album_of(flow, flow->target));
    }
}

static int artist_position(const CoverFlowScreen *flow, uint16_t album)
{
    for (int position = 0; position < flow->count; position++) {
        if (album_of(flow, position) == album) {
            return position;
        }
    }
    return NO_POSITION;
}

static int playing_position(const CoverFlowScreen *flow)
{
    PlayerStatus status = player_status(flow->config.player);
    LibraryTrack track;
    if (!status.has_track || !library_read_track(flow->config.library, status.track, &track)) {
        return NO_POSITION;
    }
    if (is_artist(flow)) {
        return artist_position(flow, track.album);
    }
    uint32_t position = 0;
    if (!cover_flow_order_find(flow->config.library, flow->order, track.album, &position)) {
        return NO_POSITION;
    }
    return (int)position + flow->first;
}

static int scroll_count(const void *context)
{
    const CoverFlowScreen *flow = context;
    return flow->count;
}

static int scroll_section(const void *context, int position)
{
    const CoverFlowScreen *flow = context;
    const Library *library = flow->config.library;
    LibraryAlbum album;
    LibraryGroup artist;
    if (!library_read_album(library, album_of(flow, position), &album)) {
        return LIST_SECTION_BEFORE_LETTERS;
    }
    uint32_t name = album.title;
    if (flow->order.linked) {
        if (!library_read_group(library, LIBRARY_ALBUM_ARTISTS, album.album_artist, &artist)) {
            return LIST_SECTION_BEFORE_LETTERS;
        }
        name = artist.name;
    }
    char text[COVER_FLOW_TEXT_CAPACITY];
    if (!library_read_string(library, name, text, sizeof text)) {
        return LIST_SECTION_BEFORE_LETTERS;
    }
    char initial[2] = {sort_key_initial(text), '\0'};
    return list_section_of_key(initial);
}

static void move_target(CoverFlowScreen *flow, Navigation navigation)
{
    if (flow->count == 0) {
        return;
    }
    ListModel model = {
        .context = flow, .count = scroll_count, .section = is_artist(flow) ? NULL : scroll_section};
    ListView scroll = {.model = &model,
                       .selected = flow->target,
                       .visible_rows = 1,
                       .scroll_started_ms = flow->scroll_started_ms,
                       .section_shown_until_ms = flow->section_shown_until_ms};
    int direction = navigation.intent == NAVIGATION_NEXT ? 1 : -1;
    if (navigation.repeated) {
        list_view_keep_scrolling(&scroll, direction, flow->now_ms);
    } else {
        scroll.scroll_started_ms = flow->now_ms;
        list_view_move(&scroll, direction, false, flow->now_ms);
    }
    flow->target = scroll.selected;
    flow->scroll_started_ms = scroll.scroll_started_ms;
    flow->section_shown_until_ms = scroll.section_shown_until_ms;
}

static ScreenAction play_random(CoverFlowScreen *flow)
{
    Random random = random_seeded(hal_clock_microseconds());
    uint32_t pick = random_below(&random, flow->order.count);
    uint16_t album = LIBRARY_NONE;
    BrowseQuery child;
    BrowseItem item = {.kind = BROWSE_ITEM_ALBUM, .group = LIBRARY_ALBUMS, .index = 0};
    if (!cover_flow_order_album(flow->config.library, flow->order, pick, &album)) {
        return screen_action_none();
    }
    item.index = album;
    PlayerListStart start = {.position = 0, .seed = random_next(&random), .in_order = true};
    if (!browse_child(browse_root_query(BROWSE_ALBUMS), item, &child)) {
        return screen_action_none();
    }
    start.source = child;
    if (!player_play_from(flow->config.player, start)) {
        return screen_action_none();
    }
    flow->target = (int)pick + flow->first;
    return screen_action_push(flow->config.browse->config.now_playing);
}

static ScreenAction open_album(CoverFlowScreen *flow)
{
    if (flow->count > 0 && is_random(flow, flow->target)) {
        return play_random(flow);
    }
    bool artist = is_artist(flow);
    BrowseQuery parent = artist ? flow->albums : browse_root_query(BROWSE_ALBUMS);
    BrowseQuery child;
    BrowseItem item = {
        .kind = BROWSE_ITEM_ALBUM, .group = LIBRARY_ALBUMS, .index = album_of(flow, flow->target)};
    if (flow->count == 0 || !browse_child(parent, item, &child)) {
        return screen_action_none();
    }
    int level = artist ? flow->parent->level + 1 : 0;
    Screen *album = browse_pool_open(flow->config.browse, level, child);
    return album != NULL ? screen_action_push(album) : screen_action_none();
}

static ScreenAction toggle_favorite(CoverFlowScreen *flow)
{
    uint16_t album = album_of(flow, flow->target);
    if (flow->count == 0 || album == LIBRARY_NONE) {
        return screen_action_none();
    }
    FavoriteResult result = favorites_store_toggle_album(flow->config.favorites, album);
    toast_show(flow->config.toast, favorites_store_message(result), flow->now_ms);
    refresh_text(flow, true);
    return screen_action_none();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    CoverFlowScreen *flow = context;
    flow->now_ms = now_ms;
    Navigation navigation = navigation_from_key(event);
    switch (navigation.intent) {
    case NAVIGATION_PREVIOUS:
    case NAVIGATION_NEXT:
        move_target(flow, navigation);
        return screen_action_none();
    case NAVIGATION_SELECT:
        return open_album(flow);
    case NAVIGATION_OPTIONS:
        return toggle_favorite(flow);
    case NAVIGATION_BACK:
        return screen_action_pop();
    case NAVIGATION_HOME:
        return screen_action_pop_to_root();
    case NAVIGATION_NONE:
        return screen_action_none();
    }
    return screen_action_none();
}

static void repaint_placeholders(CoverFlowScreen *flow)
{
    const Theme *theme = flow->config.theme;
    uint32_t palette = hash_update(HASH_START, &theme->colors, sizeof theme->colors);
    if (flow->art_palette != palette) {
        cover_placeholders_paint(flow->config.covers, theme, COVER_PLACEHOLDER);
        flow->art_palette = palette;
    }
}

static void choose_albums(CoverFlowScreen *flow)
{
    const Library *library = flow->config.library;
    LibraryGroup group;
    flow->order = (CoverFlowOrder){.first_link = 0, .count = 0, .linked = true};
    if (library_read_group(library, flow->albums.group, flow->albums.subject, &group)) {
        flow->order.first_link = group.first_link;
        flow->order.count = group.link_count;
    }
    flow->count = (int)flow->order.count;
    flow->artist[0] = '\0';
    browse_title(library, flow->albums, flow->artist, sizeof flow->artist);
}

static void choose_all_albums(CoverFlowScreen *flow)
{
    flow->first = RANDOM_SLOTS;
    flow->order = cover_flow_order_make(flow->config.library);
    flow->count = flow->order.count > 0 ? (int)flow->order.count + flow->first : 0;
}

static void show_hint(CoverFlowScreen *flow, uint32_t now_ms)
{
    if (flow->hinted || flow->count == 0 || flow->config.toast == NULL) {
        return;
    }
    StringId hint = is_artist(flow) ? STR_ARTIST_ALBUMS_HINT : STR_COVER_FLOW_HINT;
    toast_show(flow->config.toast, i18n_text(hint), now_ms);
    flow->hinted = true;
}

static void enter(void *context, uint32_t now_ms)
{
    CoverFlowScreen *flow = context;
    if (!flow->positioned) {
        if (is_artist(flow)) {
            choose_albums(flow);
        } else {
            choose_all_albums(flow);
        }
        int playing = playing_position(flow);
        flow->target = playing != NO_POSITION ? playing : min_of(flow->first, flow->count - 1);
        flow->positioned = true;
        flow->screen.fast_list = flow->count > 1;
        show_hint(flow, now_ms);
    }
    flow->section_shown_until_ms = 0;
    flow->position = (int32_t)flow->target * COVER_FLOW_ONE;
    flow->last_ms = now_ms;
    flow->now_ms = now_ms;
    refresh_text(flow, true);
    repaint_placeholders(flow);
}

static void update(void *context, uint32_t now_ms)
{
    CoverFlowScreen *flow = context;
    int32_t target = (int32_t)flow->target * COVER_FLOW_ONE;
    flow->position = cover_flow_approach(flow->position, target, now_ms - flow->last_ms);
    flow->last_ms = now_ms;
    flow->now_ms = now_ms;
    load_covers(flow);
    cover_placeholders_paint(flow->config.covers, flow->config.theme, COVER_MISSING);
    refresh_text(flow, false);
}

static bool shown(CoverState state)
{
    return state == COVER_LOADED || state == COVER_PLACEHOLDER;
}

static bool cover_image(const CoverFlowScreen *flow, int position, Image *image)
{
    if (is_random(flow, position)) {
        *image = shuffle_image();
        return true;
    }
    uint16_t album = album_of(flow, position);
    return shown(cover_cache_full(flow->config.covers, album, image)) ||
           shown(cover_cache_half(flow->config.covers, position, album, image));
}

static void draw_cover(const CoverFlowScreen *flow, Canvas *canvas, int position)
{
    int32_t relative = (int32_t)position * COVER_FLOW_ONE - flow->position;
    bool stacked = theme_is_portrait(flow->config.theme);
    Quad quad = stacked ? cover_flow_stacked_quad(&BOARD_COVER_LAYOUT.cover_stack, relative)
                        : cover_flow_quad(&BOARD_COVER_LAYOUT.cover_flow, relative);
    QuadLight light = cover_flow_light(&BOARD_COVER_LAYOUT.cover_flow, relative);
    if (light.left == 0 && light.right == 0) {
        return;
    }
    if (stacked) {
        light.left = light.right = (uint16_t)max_of(light.left, light.right);
    }
    Image image;
    if (cover_image(flow, position, &image)) {
        uint32_t started_us = hal_clock_microseconds();
        if (is_random(flow, position)) {
            image_draw_quad_smooth(canvas, image, quad, light);
        } else {
            image_draw_quad_lit(canvas, image, quad, light);
        }
        uint32_t reflection_us = hal_clock_microseconds();
        draw_profile_add(DRAW_PART_COVERS, reflection_us - started_us);
        if (stacked) {
            return;
        }
        QuadLight reflected = {(uint16_t)(light.left * REFLECTION_LIGHT / COLOR_LIGHT_FULL),
                               (uint16_t)(light.right * REFLECTION_LIGHT / COLOR_LIGHT_FULL)};
        image_draw_reflection(canvas, image, quad, reflected,
                              board_cover_flow_text_layout(flow->config.theme)->reflection_rows);
        draw_profile_add(DRAW_PART_REFLECTIONS, hal_clock_microseconds() - reflection_us);
    } else {
        image_fill_quad(canvas, quad, flow->config.theme->colors.divider);
    }
}

static void draw_line(const CoverFlowScreen *flow, Canvas *canvas, Label label, TextStyle style)
{
    int width = text_width(style.font, label.text);
    if (width <= label.area.width) {
        label_draw(canvas, style,
                   (Label){label_centered_area(style.font, label.area, label.text), label.text});
        return;
    }
    MarqueeTiming timing = flow->config.theme->marquee;
    int offset =
        marquee_offset(timing, width, label.area.width, flow->now_ms - flow->text_changed_ms);
    label_draw_scrolling(canvas, style, label, (Marquee){.offset = offset, .gap = timing.gap_px});
}

static void draw_text(const CoverFlowScreen *flow, Canvas *canvas)
{
    const Theme *theme = flow->config.theme;
    const CoverFlowTextLayout *layout = board_cover_flow_text_layout(theme);
    int padding = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * padding;
    Color ink = theme->colors.header_text;
    TextStyle title = {.font = theme->fonts.title, .color = ink};
    TextStyle artist = {.font = theme->fonts.body,
                        .color = color_blend(BACKDROP, ink, DETAIL_ALPHA)};
    Rect title_area = {padding, layout->title_top, width, layout->line_height};
    Rect artist_area = {padding, layout->artist_top, width, layout->line_height};
    draw_line(flow, canvas, (Label){title_area, flow->title}, title);
    const char *detail = is_artist(flow) ? flow->year : flow->artist;
    draw_line(flow, canvas, (Label){artist_area, detail}, artist);
}

static void draw_section(const CoverFlowScreen *flow, Canvas *canvas)
{
    if (flow->section_shown_until_ms == 0 ||
        (int32_t)(flow->now_ms - flow->section_shown_until_ms) >= 0) {
        return;
    }
    const Theme *theme = flow->config.theme;
    int padding = theme->metrics.padding;
    int height = font_line_height(theme->fonts.title);
    Rect badge = {padding, padding, height + 2 * padding, height + padding};
    const char *letter = list_section_label(scroll_section(flow, flow->target));
    canvas_fill_rect(canvas, badge, theme->colors.selection_background);
    label_draw(canvas, (TextStyle){theme->fonts.title, theme->colors.selection_text},
               (Label){label_centered_area(theme->fonts.title, badge, letter), letter});
}

static void draw_empty(const Theme *theme, Canvas *canvas)
{
    Rect screen = {0, 0, theme->metrics.screen_width, theme->metrics.screen_height};
    message_draw(canvas, theme, screen, i18n_text(STR_NOTHING_HERE));
}

static void draw_backdrop(const Theme *theme, Canvas *canvas)
{
    int floor =
        theme_is_portrait(theme)
            ? board_cover_flow_text_layout(theme)->title_top - FLOOR_GAP
            : BOARD_COVER_LAYOUT.cover_flow.center_top + BOARD_COVER_LAYOUT.cover_flow.center_size;
    int height = theme->metrics.screen_height;
    canvas_fill_rect(canvas, (Rect){0, 0, theme->metrics.screen_width, floor}, BACKDROP);
    for (int y = floor; y < height; y++) {
        uint8_t level = (uint8_t)(FLOOR_LEVEL * (height - y) / (height - floor));
        canvas_fill_rect(canvas, (Rect){0, y, theme->metrics.screen_width, 1},
                         color_from_rgb(level, level, level));
    }
}

static void draw(void *context, Canvas *canvas)
{
    const CoverFlowScreen *flow = context;
    uint32_t backdrop_started_us = hal_clock_microseconds();
    draw_backdrop(flow->config.theme, canvas);
    draw_profile_add(DRAW_PART_BACKDROP, hal_clock_microseconds() - backdrop_started_us);
    if (flow->count == 0) {
        draw_empty(flow->config.theme, canvas);
        return;
    }
    int order[COVER_FLOW_MAX_VISIBLE];
    int visible = cover_flow_draw_order(flow->position, flow->count, order);
    for (int index = 0; index < visible; index++) {
        draw_cover(flow, canvas, order[index]);
    }
    uint32_t text_started_us = hal_clock_microseconds();
    draw_text(flow, canvas);
    draw_section(flow, canvas);
    draw_profile_add(DRAW_PART_TEXT, hal_clock_microseconds() - text_started_us);
}

Screen *cover_flow_screen_init(CoverFlowScreen *flow, CoverFlowConfig config)
{
    bool artist = config.artist_keys != NULL;
    flow->config = config;
    flow->order = (CoverFlowOrder){0};
    flow->first = 0;
    flow->count = 0;
    flow->keys = handle_key;
    flow->target = 0;
    flow->position = 0;
    flow->positioned = false;
    flow->hinted = false;
    flow->text_position = NO_POSITION;
    flow->art_palette = 0;
    flow->scroll_started_ms = 0;
    flow->section_shown_until_ms = 0;
    flow->title[0] = '\0';
    flow->artist[0] = '\0';
    flow->year[0] = '\0';
    flow->screen = (Screen){
        .title = artist ? flow->artist : i18n_text(STR_COVER_FLOW),
        .context = flow,
        .full_screen = true,
        .fast_list = flow->count > 1,
        .paints_every_pixel = true,
        .enter = enter,
        .handle_key = artist ? config.artist_keys : handle_key,
        .update = update,
        .draw = draw,
    };
    return &flow->screen;
}
