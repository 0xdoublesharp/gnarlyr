#include "cover_flow_screen.h"

#include <stddef.h>

#include "board_layout.h"
#include "core/random.h"
#include "core/string_builder.h"
#include "draw_profile.h"
#include "hal/clock.h"
#include "ui/cover_flow.h"
#include "ui/icons.h"
#include "ui/label.h"
#include "ui/marquee.h"
#include "ui/message_screen.h"
#include "ui/navigation.h"

enum {
    NO_POSITION = -1,
    REFLECTION_LIGHT = 80,
    RANDOM_POSITION = 0,
    SHUFFLE_SIZE = 48,
    SHUFFLE_ICON = 2 * ICON_LARGE_GRID,
    SIXTEENTHS = 16
};

static const char STAR_PREFIX[] = "\xE2\x98\x85 ";
static const char EMPTY_MESSAGE[] = "No albums yet \xE2\x80\x93 copy music over USB";
static const char RANDOM_TITLE[] = "Random album";
static const char RANDOM_HINT[] = "Play any album";

static Color shuffle_pixels[SHUFFLE_SIZE * SHUFFLE_SIZE];

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

static uint16_t album_of(const CoverFlowScreen *flow, int position)
{
    uint16_t album = LIBRARY_NONE;
    if (position <= RANDOM_POSITION || !cover_flow_order_album(flow->config.library, flow->order,
                                                               (uint32_t)position - 1, &album)) {
        return LIBRARY_NONE;
    }
    return album;
}

static void paint_shuffle_card(const Theme *theme)
{
    Canvas card = canvas_make(shuffle_pixels, (Rect){0, 0, SHUFFLE_SIZE, SHUFFLE_SIZE});
    canvas_clear(&card, theme->colors.selection_background);
    int inset = (SHUFFLE_SIZE - SHUFFLE_ICON) / 2;
    Color ink = theme->colors.selection_text;
    icon_draw(&card, ICON_SHUFFLE, (Rect){inset, inset, SHUFFLE_ICON, SHUFFLE_ICON},
              (IconInk){ink, ink});
}

static Image shuffle_image(void)
{
    return (Image){.pixels = shuffle_pixels, .width = SHUFFLE_SIZE, .height = SHUFFLE_SIZE};
}

static int center_of(const CoverFlowScreen *flow)
{
    return (int)((flow->position + COVER_FLOW_ONE / 2) / COVER_FLOW_ONE);
}

static void load_text(CoverFlowScreen *flow, uint16_t album_index)
{
    const Library *library = flow->config.library;
    LibraryAlbum album;
    LibraryGroup artist;
    char title[COVER_FLOW_TEXT_CAPACITY];
    flow->title[0] = '\0';
    flow->artist[0] = '\0';
    if (!library_read_album(library, album_index, &album) ||
        !library_read_string(library, album.title, title, sizeof title)) {
        return;
    }
    StringBuilder builder = string_builder_make(flow->title, sizeof flow->title);
    if (favorites_store_contains_album(flow->config.favorites, album_index)) {
        string_builder_append(&builder, STAR_PREFIX);
    }
    string_builder_append(&builder, title);
    if (library_read_group(library, LIBRARY_ALBUM_ARTISTS, album.album_artist, &artist)) {
        library_read_string(library, artist.name, flow->artist, sizeof flow->artist);
    }
}

static void refresh_text(CoverFlowScreen *flow, bool force)
{
    int center = center_of(flow);
    if (flow->count == 0 || (!force && center == flow->text_position)) {
        return;
    }
    if (center == RANDOM_POSITION) {
        StringBuilder title = string_builder_make(flow->title, sizeof flow->title);
        string_builder_append(&title, RANDOM_TITLE);
        StringBuilder hint = string_builder_make(flow->artist, sizeof flow->artist);
        string_builder_append(&hint, RANDOM_HINT);
    } else {
        load_text(flow, album_of(flow, center));
    }
    flow->text_position = center;
    flow->text_changed_ms = flow->now_ms;
}

static bool load_nearest_half(CoverFlowScreen *flow)
{
    int center = center_of(flow);
    for (int distance = 0; distance <= COVER_CACHE_SLOTS / 2; distance++) {
        for (int side = -1; side <= 1; side += 2) {
            int position = center + side * distance;
            if (position > RANDOM_POSITION && position < flow->count &&
                cover_cache_load_half(flow->config.covers, position, album_of(flow, position))) {
                return true;
            }
        }
    }
    return false;
}

static void load_covers(CoverFlowScreen *flow)
{
    if (flow->count == 0 || load_nearest_half(flow)) {
        return;
    }
    if (flow->target != RANDOM_POSITION &&
        flow->position == (int32_t)flow->target * COVER_FLOW_ONE) {
        cover_cache_load_full(flow->config.covers, album_of(flow, flow->target));
    }
}

static int playing_position(const CoverFlowScreen *flow)
{
    PlayerStatus status = player_status(flow->config.player);
    LibraryTrack track;
    if (!status.has_track || !library_read_track(flow->config.library, status.track, &track)) {
        return NO_POSITION;
    }
    uint32_t position = 0;
    if (!cover_flow_order_find(flow->config.library, flow->order, track.album, &position)) {
        return NO_POSITION;
    }
    return (int)position + 1;
}

static void move_target(CoverFlowScreen *flow, int delta)
{
    flow->target = max_of(0, min_of(flow->count - 1, flow->target + delta));
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
    flow->target = (int)pick + 1;
    return screen_action_push(flow->config.browse->config.now_playing);
}

static ScreenAction open_album(CoverFlowScreen *flow)
{
    if (flow->count > 0 && flow->target == RANDOM_POSITION) {
        return play_random(flow);
    }
    BrowseQuery child;
    BrowseItem item = {
        .kind = BROWSE_ITEM_ALBUM, .group = LIBRARY_ALBUMS, .index = album_of(flow, flow->target)};
    if (flow->count == 0 || !browse_child(browse_root_query(BROWSE_ALBUMS), item, &child)) {
        return screen_action_none();
    }
    Screen *album = browse_pool_open(flow->config.browse, 0, child);
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
    switch (navigation_from_key(event).intent) {
    case NAVIGATION_PREVIOUS:
        move_target(flow, -1);
        return screen_action_none();
    case NAVIGATION_NEXT:
        move_target(flow, 1);
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

static void enter(void *context, uint32_t now_ms)
{
    CoverFlowScreen *flow = context;
    if (!flow->positioned) {
        int playing = playing_position(flow);
        flow->target = playing != NO_POSITION ? playing : min_of(1, flow->count - 1);
        flow->positioned = true;
    }
    flow->position = (int32_t)flow->target * COVER_FLOW_ONE;
    flow->last_ms = now_ms;
    flow->now_ms = now_ms;
    paint_shuffle_card(flow->config.theme);
    refresh_text(flow, true);
}

static void update(void *context, uint32_t now_ms)
{
    CoverFlowScreen *flow = context;
    int32_t target = (int32_t)flow->target * COVER_FLOW_ONE;
    flow->position = cover_flow_approach(flow->position, target, now_ms - flow->last_ms);
    flow->last_ms = now_ms;
    flow->now_ms = now_ms;
    load_covers(flow);
    refresh_text(flow, false);
}

static bool cover_image(const CoverFlowScreen *flow, int position, Image *image)
{
    if (position == RANDOM_POSITION) {
        *image = shuffle_image();
        return true;
    }
    uint16_t album = album_of(flow, position);
    return cover_cache_full(flow->config.covers, album, image) == COVER_LOADED ||
           cover_cache_half(flow->config.covers, position, album, image) == COVER_LOADED;
}

static int quad_edge(int from, int to, int sixteenths)
{
    return from + (to - from) * sixteenths / SIXTEENTHS;
}

static Quad quad_part(Quad quad, int top, int bottom, int inset)
{
    int far = SIXTEENTHS - inset;
    int left_top = quad_edge(quad.left_top, quad.right_top, inset);
    int left_height = quad_edge(quad.left_height, quad.right_height, inset);
    int right_top = quad_edge(quad.left_top, quad.right_top, far);
    int right_height = quad_edge(quad.left_height, quad.right_height, far);
    return (Quad){quad_edge(quad.left, quad.left + quad.width, inset),
                  quad.width * (far - inset) / SIXTEENTHS,
                  left_top + left_height * top / SIXTEENTHS,
                  left_height * (bottom - top) / SIXTEENTHS,
                  right_top + right_height * top / SIXTEENTHS,
                  right_height * (bottom - top) / SIXTEENTHS};
}

static void draw_placeholder(const CoverFlowScreen *flow, Canvas *canvas, Quad quad,
                             QuadLight light)
{
    const ThemeColors *colors = &flow->config.theme->colors;
    unsigned shade = (light.left + light.right) / 2u;
    image_fill_quad(canvas, quad, color_scale(colors->divider, shade));
    image_fill_quad(canvas, quad_part(quad, 3, 11, 3),
                    color_scale(colors->selection_background, shade));
}

static void draw_cover(const CoverFlowScreen *flow, Canvas *canvas, int position)
{
    int32_t relative = (int32_t)position * COVER_FLOW_ONE - flow->position;
    Quad quad = cover_flow_quad(&BOARD_LAYOUT.cover_flow, relative);
    QuadLight light = cover_flow_light(&BOARD_LAYOUT.cover_flow, relative);
    if (light.left == 0 && light.right == 0) {
        return;
    }
    Image image;
    if (cover_image(flow, position, &image)) {
        uint32_t started_us = hal_clock_microseconds();
        if (position == RANDOM_POSITION) {
            image_draw_quad_smooth(canvas, image, quad, light);
        } else {
            image_draw_quad_lit(canvas, image, quad, light);
        }
        uint32_t reflection_us = hal_clock_microseconds();
        draw_profile_add(DRAW_PART_COVERS, reflection_us - started_us);
        QuadLight reflected = {(uint16_t)(light.left * REFLECTION_LIGHT / COLOR_LIGHT_FULL),
                               (uint16_t)(light.right * REFLECTION_LIGHT / COLOR_LIGHT_FULL)};
        image_draw_reflection(canvas, image, quad, reflected,
                              BOARD_LAYOUT.cover_flow_text.reflection_rows);
        draw_profile_add(DRAW_PART_REFLECTIONS, hal_clock_microseconds() - reflection_us);
    } else {
        draw_placeholder(flow, canvas, quad, light);
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
    const CoverFlowTextLayout *layout = &BOARD_LAYOUT.cover_flow_text;
    int padding = theme->metrics.padding;
    int width = theme->metrics.screen_width - 2 * padding;
    TextStyle title = {.font = theme->fonts.title, .color = theme->colors.header_text};
    TextStyle artist = {.font = theme->fonts.body, .color = theme->colors.header_text};
    Rect title_area = {padding, layout->title_top, width, layout->line_height};
    Rect artist_area = {padding, layout->artist_top, width, layout->line_height};
    draw_line(flow, canvas, (Label){title_area, flow->title}, title);
    draw_line(flow, canvas, (Label){artist_area, flow->artist}, artist);
}

static void draw_empty(const Theme *theme, Canvas *canvas)
{
    Rect screen = {0, 0, theme->metrics.screen_width, theme->metrics.screen_height};
    canvas_fill_rect(canvas, screen, theme->colors.background);
    message_draw_card(canvas, theme, screen, (MessageCard){ICON_COVER_FLOW, EMPTY_MESSAGE});
}

static void draw_backdrop(const Theme *theme, Canvas *canvas)
{
    int floor = BOARD_LAYOUT.cover_flow.center_top + BOARD_LAYOUT.cover_flow.center_size;
    int height = theme->metrics.screen_height;
    canvas_fill_rect(canvas, (Rect){0, 0, theme->metrics.screen_width, floor},
                     theme->colors.header_background);
    canvas_fill_gradient(canvas, (Rect){0, floor, theme->metrics.screen_width, height - floor},
                         theme->colors.header_top, theme->colors.header_background);
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
    draw_profile_add(DRAW_PART_TEXT, hal_clock_microseconds() - text_started_us);
}

Screen *cover_flow_screen_init(CoverFlowScreen *flow, CoverFlowConfig config)
{
    flow->config = config;
    flow->order = cover_flow_order_make(config.library);
    flow->count = flow->order.count > 0 ? (int)flow->order.count + 1 : 0;
    flow->target = 0;
    flow->position = 0;
    flow->positioned = false;
    flow->text_position = NO_POSITION;
    flow->title[0] = '\0';
    flow->artist[0] = '\0';
    flow->screen = (Screen){
        .title = "Cover Flow",
        .context = flow,
        .full_screen = true,
        .paints_every_pixel = true,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &flow->screen;
}
