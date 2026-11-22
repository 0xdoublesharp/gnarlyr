#include "home_art.h"

#include "core/deadline.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "gfx/image.h"
#include "ui/icons.h"
#include "ui/label.h"

enum {
    COVER_MARGIN_TOP = 8,
    CAPTION_ROOM = 62,
    SMALLEST_COVER = 64,
    CUE_HEIGHT = 48,
    CUE_LINE = 20,
    CUE_MARK_WIDTH = 13,
    CUE_MARK_HEIGHT = 8,
    CUE_MARK_STROKE = 3,
    NUMBER_CAPACITY = 8
};

static void update_metadata(HomeArt *art)
{
    PlayerStatus status = player_status(art->player);
    if (art->metadata_known && art->metadata_track == status.has_track &&
        (status.has_track ? art->shown_track == status.track : art->shown_album == art->album)) {
        return;
    }
    art->metadata_known = true;
    art->metadata_track = status.has_track;
    art->shown_track = status.track;
    art->shown_album = art->album;
    art->title[0] = '\0';
    art->artist[0] = '\0';
    LibraryTrack track;
    LibraryAlbum album;
    LibraryGroup artist;
    const Library *library = &art->store->library;
    bool has_track = status.has_track && library_read_track(library, status.track, &track);
    bool has_album = library_read_album(library, art->album, &album);
    if (has_track || has_album) {
        library_read_string(library, has_track ? track.title : album.title, art->title,
                            sizeof art->title);
        uint16_t id = has_track ? track.artist : album.album_artist;
        LibrarySection section = has_track ? LIBRARY_ARTISTS : LIBRARY_ALBUM_ARTISTS;
        if (library_read_group(library, section, id, &artist)) {
            library_read_string(library, artist.name, art->artist, sizeof art->artist);
        }
    }
}

static uint32_t album_count(const HomeArt *art)
{
    return art->store->available ? library_music_count(&art->store->library, LIBRARY_ALBUMS) : 0;
}

static bool playing_album(const HomeArt *art, uint16_t *album)
{
    PlayerStatus status = player_status(art->player);
    LibraryTrack track;
    if (!status.has_track || !library_read_track(&art->store->library, status.track, &track)) {
        return false;
    }
    *album = track.album;
    return true;
}

static bool cover_missing(const HomeArt *art, uint16_t album)
{
    Image ignored;
    cover_cache_load_full(art->covers, album);
    return cover_cache_full(art->covers, album, &ignored) != COVER_LOADED;
}

static void show_next_with_a_cover(HomeArt *art, uint32_t count)
{
    for (uint32_t tried = 0; tried < count; tried++) {
        art->album = (uint16_t)((art->album + 1) % count);
        if (!cover_missing(art, art->album)) {
            return;
        }
    }
}

static void cycle(HomeArt *art, uint32_t count, uint32_t now_ms)
{
    if (!art->covers->available) {
        art->album = 0;
        return;
    }
    if (!art->cycling) {
        art->cycling = true;
        art->album = (uint16_t)(count - 1);
        show_next_with_a_cover(art, count);
        art->next_change_ms = now_ms + HOME_ART_CYCLE_MS;
        return;
    }
    if (deadline_reached(now_ms, art->next_change_ms)) {
        show_next_with_a_cover(art, count);
        art->next_change_ms = now_ms + HOME_ART_CYCLE_MS;
    }
}

void home_art_update(void *context, uint32_t now_ms)
{
    HomeArt *art = context;
    uint32_t count = album_count(art);
    art->has_album = count > 0;
    if (!art->has_album) {
        return;
    }
    uint16_t album;
    if (playing_album(art, &album)) {
        art->cycling = false;
        art->album = album;
    } else {
        cycle(art, count, now_ms);
    }
    cover_cache_load_full(art->covers, art->album);
    update_metadata(art);
}

static void draw_caption(const HomeArt *art, Canvas *canvas, Rect area)
{
    const Theme *theme = art->theme;
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text};
    Rect caption = {area.x, area.y, area.width, 2 * font_line_height(style.font)};
    canvas_fill_rect(canvas, caption, theme->colors.backdrop_bottom);
    label_draw(canvas, style,
               (Label){label_centered_area(style.font, area, art->title), art->title});
    area.y += font_line_height(style.font);
    style.color = theme->colors.text_secondary;
    label_draw(canvas, style,
               (Label){label_centered_area(style.font, area, art->artist), art->artist});
    PlayerStatus status = player_status(art->player);
    if (status.has_track && status.duration_ms != 0) {
        Rect progress = {area.x + 4, area.y + 24, area.width - 8, 2};
        canvas_fill_rect(canvas, progress, theme->colors.divider);
        uint32_t position =
            status.position_ms < status.duration_ms ? status.position_ms : status.duration_ms;
        if (status.state == PLAYER_PLAYING && position < status.duration_ms) {
            position = position / 1000 * 1000;
        }
        progress.width = (int)((uint64_t)progress.width * position / status.duration_ms);
        canvas_fill_rect(canvas, progress, theme->colors.accent);
    }
}

static int hidden_below(const HomeArt *art)
{
    const ListView *list = art->list;
    if (list == NULL || list->model == NULL) {
        return 0;
    }
    return list->model->count(list->model->context) - list->top - list->visible_rows;
}

static void draw_turned_chevron(Canvas *canvas, Rect box, Color ink)
{
    for (int row = 0; row < CUE_MARK_HEIGHT; row++) {
        int inset = row * (CUE_MARK_WIDTH - CUE_MARK_STROKE) / (2 * CUE_MARK_HEIGHT);
        canvas_fill_rect(canvas, (Rect){box.x + inset, box.y + row, CUE_MARK_STROKE, 1}, ink);
        canvas_fill_rect(canvas,
                         (Rect){box.x + CUE_MARK_WIDTH - inset - CUE_MARK_STROKE, box.y + row,
                                CUE_MARK_STROKE, 1},
                         ink);
    }
}

static void draw_cue(const HomeArt *art, Canvas *canvas, Rect area)
{
    const Theme *theme = art->theme;
    const ListView *list = art->list;
    const ListModel *model = list->model;
    char numbers[3][NUMBER_CAPACITY];
    const char *values[3];
    int range[3] = {list->top + 1, list->top + list->visible_rows, model->count(model->context)};
    for (int index = 0; index < 3; index++) {
        StringBuilder number = string_builder_make(numbers[index], NUMBER_CAPACITY);
        string_builder_append_unsigned(&number, (uint32_t)range[index]);
        values[index] = numbers[index];
    }
    char text[3 * NUMBER_CAPACITY + 16];
    StringBuilder line = string_builder_make(text, sizeof text);
    i18n_format(&line, STR_RANGE_OF, values, 3);
    canvas_fill_rect(canvas, area, theme->colors.header_background);
    canvas_fill_rect(canvas, (Rect){area.x, area.y, area.width, 1}, theme->colors.divider);
    int padding = theme->metrics.padding;
    Rect row = {area.x + padding, area.y + 4, area.width - 2 * padding, CUE_LINE};
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.text_secondary, false},
               (Label){row, text});
    row.y += CUE_LINE;
    draw_turned_chevron(canvas, (Rect){row.x, row.y + (CUE_LINE - CUE_MARK_HEIGHT) / 2, 0, 0},
                        theme->colors.accent);
    row.x += CUE_MARK_WIDTH + 4;
    row.width -= CUE_MARK_WIDTH + 4;
    label_draw(canvas, (TextStyle){theme->fonts.small, theme->colors.accent, false},
               (Label){row, model->label(model->context, list->top + list->visible_rows)});
}

static void draw_backdrop(const HomeArt *art, Canvas *canvas, Rect area, int floor)
{
    Rect sky = area;
    if (theme_is_portrait(art->theme) && floor > area.y) {
        sky.height = 2 * (floor - area.y);
    }
    Canvas band = canvas_clipped(canvas, area);
    home_art_draw_backdrop(&band, sky, art->theme);
}

void home_art_draw(void *context, Canvas *canvas, Rect area)
{
    const HomeArt *art = context;
    const Theme *theme = art->theme;
    bool below = theme_is_portrait(theme);
    Rect divider =
        below ? (Rect){area.x, area.y, area.width, 1} : (Rect){area.x, area.y, 1, area.height};
    int cue = hidden_below(art) > 0 ? CUE_HEIGHT : 0;
    area.height -= cue;
    int size = area.height - CAPTION_ROOM;
    bool captions = size >= SMALLEST_COVER;
    size = captions && size < COVER_FULL_SIZE ? size : COVER_FULL_SIZE;
    int left = area.x + (area.width - size) / 2;
    int top = area.y + (below && !captions ? (area.height - size) / 2 : COVER_MARGIN_TOP);
    draw_backdrop(art, canvas, area, captions ? top + size + CAPTION_ROOM - COVER_MARGIN_TOP : 0);
    canvas_fill_rect(canvas, divider, theme->colors.divider);
    Image cover;
    if (art->has_album && cover_cache_full(art->covers, art->album, &cover) == COVER_LOADED) {
        image_draw_quad(canvas, cover, (Quad){left, size, top, size, top, size});
    } else {
        icon_draw(canvas, ICON_MIXTAPE,
                  (Rect){left + size / 7, top + size / 7, size * 5 / 7, size * 5 / 7},
                  (IconInk){theme->colors.text_secondary, theme->colors.accent});
    }
    if (captions) {
        draw_caption(art, canvas, (Rect){area.x + 10, top + size + 6, area.width - 20, 18});
    }
    if (cue > 0) {
        draw_cue(art, canvas, (Rect){area.x, area.y + area.height, area.width, cue});
    }
}
