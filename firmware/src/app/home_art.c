#include "home_art.h"

#include "gfx/image.h"
#include "ui/icons.h"
#include "ui/label.h"

enum {
    COVER_MARGIN_TOP = 8
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

void home_art_init(HomeArt *art, CoverCache *covers, const LibraryStore *store, Player *player,
                   const Theme *theme)
{
    *art = (HomeArt){.covers = covers, .store = store, .player = player, .theme = theme};
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
    return cover_cache_full(art->covers, album, &ignored) == COVER_MISSING;
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
    if (!art->cycling) {
        art->cycling = true;
        art->album = (uint16_t)(count - 1);
        show_next_with_a_cover(art, count);
        art->next_change_ms = now_ms + HOME_ART_CYCLE_MS;
        return;
    }
    if ((int32_t)(now_ms - art->next_change_ms) >= 0) {
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

static void draw_horizon(Canvas *canvas, Rect area, const Theme *theme)
{
    if (!theme->horizon) {
        return;
    }
    int top = area.y + area.height / 2;
    int height = area.y + area.height - top;
    Color ink = color_between(theme->colors.divider, theme->colors.accent, 1, 3);
    for (int line = 0; line < 7; line++) {
        int y = top + line * line * height / 36;
        canvas_fill_rect(canvas, (Rect){area.x, y, area.width, 1}, ink);
    }
    for (int ray = -5; ray <= 5; ray++) {
        for (int y = 0; y < height; y++) {
            int x = area.x + area.width / 2 + ray * (6 + y * area.width / (height * 5));
            canvas_fill_rect(canvas, (Rect){x, top + y, 1, 1}, ink);
        }
    }
}

void home_art_draw_backdrop(Canvas *canvas, Rect area, const Theme *theme)
{
    canvas_fill_gradient(canvas, area, theme->colors.backdrop_top, theme->colors.backdrop_bottom);
    draw_horizon(canvas, area, theme);
}

static void draw_caption(const HomeArt *art, Canvas *canvas, Rect area)
{
    const Theme *theme = art->theme;
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text};
    label_draw(canvas, style,
               (Label){label_centered_area(style.font, area, art->title), art->title});
    area.y += 18;
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

void home_art_draw(void *context, Canvas *canvas, Rect area)
{
    const HomeArt *art = context;
    const Theme *theme = art->theme;
    bool below = theme_is_portrait(theme);
    Rect divider =
        below ? (Rect){area.x, area.y, area.width, 1} : (Rect){area.x, area.y, 1, area.height};
    home_art_draw_backdrop(canvas, area, theme);
    canvas_fill_rect(canvas, divider, theme->colors.divider);
    bool captions = area.height >= COVER_FULL_SIZE + 70;
    int left = area.x + (area.width - COVER_FULL_SIZE) / 2;
    int top =
        area.y + (below && !captions ? (area.height - COVER_FULL_SIZE) / 2 : COVER_MARGIN_TOP);
    Image cover;
    if (art->has_album && cover_cache_full(art->covers, art->album, &cover) == COVER_LOADED) {
        Quad quad = {left, COVER_FULL_SIZE, top, COVER_FULL_SIZE, top, COVER_FULL_SIZE};
        image_draw_quad(canvas, cover, quad);
    } else {
        icon_draw(canvas, ICON_MIXTAPE, (Rect){left + 16, top + 16, 80, 80},
                  (IconInk){theme->colors.text_secondary, theme->colors.accent});
    }
    if (captions) {
        draw_caption(art, canvas,
                     (Rect){area.x + 10, top + COVER_FULL_SIZE + 6, area.width - 20, 18});
    }
}
