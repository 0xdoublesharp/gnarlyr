#include "cover_placeholders.h"

#include <stdbool.h>
#include <stddef.h>

#include "fonts/fonts.h"
#include "ui/placeholder_art.h"

enum {
    NAME_CAPACITY = 128
};

static const ArtFonts ART_FONTS = {&FONT_INTER_SEMIBOLD_26, &FONT_INTER_SEMIBOLD_22};

static bool same_text(const char *first, const char *second)
{
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

static bool read_name(const Library *library, uint32_t string, char *text, const char *unknown)
{
    text[0] = '\0';
    return library_read_string(library, string, text, NAME_CAPACITY) && text[0] != '\0' &&
           !same_text(text, unknown);
}

static ArtInitials initials_of(const Library *library, uint16_t album)
{
    LibraryAlbum record;
    LibraryGroup artist;
    char text[NAME_CAPACITY];
    ArtInitials initials = {.count = 0};
    if (!library_read_album(library, album, &record)) {
        return initials;
    }
    if (read_name(library, record.title, text, LIBRARY_UNKNOWN_ALBUM)) {
        initials = placeholder_art_initials(ART_FONTS.large, text);
    }
    if (initials.count == 0 &&
        library_read_group(library, LIBRARY_ALBUM_ARTISTS, record.album_artist, &artist) &&
        read_name(library, artist.name, text, LIBRARY_UNKNOWN_ARTIST)) {
        initials = placeholder_art_initials(ART_FONTS.large, text);
    }
    return initials;
}

static void paint(const CoverCache *cache, const Theme *theme, uint16_t album, Canvas canvas)
{
    PlaceholderArt art = {
        .initials = initials_of(cache->library, album),
        .ink = placeholder_art_ink(&theme->colors, library_album_key(cache->library, album)),
        .fonts = ART_FONTS,
    };
    placeholder_art_draw(&canvas, &art);
}

static Canvas tile(Color *pixels, int size)
{
    Rect area = {0, 0, size, size};
    return (Canvas){.pixels = pixels, .bounds = area, .clip = area};
}

void cover_placeholders_paint(CoverCache *cache, const Theme *theme, CoverState from)
{
    for (int index = 0; index < COVER_CACHE_SLOTS; index++) {
        CoverSlot *slot = &cache->halves[index];
        if (slot->position >= 0 && slot->state == from) {
            paint(cache, theme, slot->album, tile(slot->pixels, COVER_HALF_SIZE));
            slot->state = COVER_PLACEHOLDER;
        }
    }
    if (cache->full_state == from) {
        paint(cache, theme, cache->full_album, tile(cache->full, COVER_FULL_SIZE));
        cache->full_state = COVER_PLACEHOLDER;
    }
}
