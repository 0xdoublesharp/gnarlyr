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
    CUE_INSET = 4,
    CUE_TEXT_CAPACITY = 32
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
        art->next_change_ms = now_ms;
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
    const char *lines[2] = {art->title, art->artist};
    Color inks[2] = {theme->colors.text, theme->colors.text_secondary};
    for (int line = 0; line < 2; line++, area.y += CUE_LINE) {
        TextStyle style = {theme->fonts.small, inks[line], false};
        label_draw(canvas, style,
                   (Label){label_centered_area(style.font, area, lines[line]), lines[line]});
    }
}

static int hidden_below(const HomeArt *art)
{
    const ListView *list = art->list;
    if (list == NULL) {
        return 0;
    }
    return list->model->count(list->model->context) - list->top - list->visible_rows;
}

static void draw_cue(const HomeArt *art, Canvas *canvas, Rect area)
{
    const Theme *theme = art->theme;
    const ListView *list = art->list;
    const ListModel *model = list->model;
    int last = list->top + list->visible_rows;
    char text[CUE_TEXT_CAPACITY];
    StringBuilder line = string_builder_make(text, sizeof text);
    i18n_format_counts(&line, STR_PLACE, (uint32_t)last, (uint32_t)model->count(model->context));
    canvas_fill_rect(canvas, area, theme->colors.header_background);
    TextStyle style = {theme->fonts.small, theme->colors.text_secondary, false};
    Rect row = {area.x + theme->metrics.padding, area.y + CUE_INSET, area.width, CUE_LINE};
    label_draw(canvas, style, (Label){row, text});
    row.y += CUE_LINE;
    style.color = theme->colors.accent;
    label_draw(canvas, style, (Label){row, model->label(model->context, last)});
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
    Rect sky = area;
    int floor = 2 * (top + size + CAPTION_ROOM - COVER_MARGIN_TOP - area.y);
    if (below && captions && floor > sky.height) {
        sky.height = floor;
    }
    home_art_draw_backdrop(canvas, sky, theme);
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
