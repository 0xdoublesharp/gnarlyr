#include "home_art.h"

#include "cassette_art.h"
#include "gfx/image.h"
#include "ui/label.h"

enum {
    COVER_MARGIN_TOP = 8
};

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
}

static void draw_caption(const HomeArt *art, Canvas *canvas, Rect area)
{
    PlayerStatus status = player_status(art->player);
    const char *caption = status.state == PLAYER_PLAYING ? "SIDE A / PLAY" : "SIDE A / PAUSE";
    if (!status.has_track) {
        caption = art->has_album ? "ALBUM COLLECTION" : "READY FOR MUSIC";
    }
    TextStyle style = {.font = art->theme->fonts.small, .color = art->theme->colors.header_text};
    Rect row = {area.x + 4, area.y + area.height - 22, area.width - 8, 18};
    label_draw(canvas, style, (Label){label_centered_area(style.font, row, caption), caption});
}

static void draw_horizon(Canvas *canvas, Rect area, const Theme *theme)
{
    int top = area.y + COVER_MARGIN_TOP + COVER_FULL_SIZE + 12;
    int bottom = area.y + area.height - 26;
    Color grid = color_blend(theme->colors.divider, theme->colors.selection_shine, 5);
    for (int step = 1; step <= 4; step++) {
        int y = top + step * step * 2;
        if (y < bottom) {
            canvas_fill_rect(canvas, (Rect){area.x + 8, y, area.width - 16, 1}, grid);
        }
    }
    int start = canvas->clip.y > top ? canvas->clip.y : top;
    int end = canvas->clip.y + canvas->clip.height;
    for (int y = start; y < bottom && y < end; y++) {
        for (int ray = -2; ray <= 2; ray++) {
            int x = area.x + area.width / 2 + ray * (y - top + 3);
            canvas_fill_rect(canvas, (Rect){x, y, 1, 1}, grid);
        }
    }
}

void home_art_draw(void *context, Canvas *canvas, Rect area)
{
    const HomeArt *art = context;
    canvas_fill_rect(canvas, area, art->theme->colors.header_background);
    canvas_fill_rect(canvas, (Rect){area.x, area.y, 1, area.height}, art->theme->colors.divider);
    draw_horizon(canvas, area, art->theme);
    int left = area.x + (area.width - COVER_FULL_SIZE) / 2;
    int top = area.y + COVER_MARGIN_TOP;
    Image cover;
    if (art->has_album && cover_cache_full(art->covers, art->album, &cover) == COVER_LOADED) {
        canvas_fill_rect(canvas,
                         (Rect){left - 2, top - 2, COVER_FULL_SIZE + 4, COVER_FULL_SIZE + 4},
                         art->theme->colors.selection_background);
        image_draw_quad(canvas, cover,
                        (Quad){left, COVER_FULL_SIZE, top, COVER_FULL_SIZE, top, COVER_FULL_SIZE});
    } else {
        PlayerStatus status = player_status(art->player);
        cassette_art_draw(canvas, (Rect){left, top + 4, COVER_FULL_SIZE, 104}, art->theme,
                          status.position_ms);
    }
    draw_caption(art, canvas, area);
}
