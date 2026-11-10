#pragma once

#include "gfx/canvas.h"

typedef enum IconId {
    ICON_NONE,
    ICON_MUSIC,
    ICON_AUDIOBOOK,
    ICON_PODCAST,
    ICON_NOW_PLAYING,
    ICON_MIXTAPE,
    ICON_AUDIO,
    ICON_SETTINGS,
    ICON_COVER_FLOW,
    ICON_ARTIST,
    ICON_ALBUM_ARTIST,
    ICON_ALBUM,
    ICON_GENRE,
    ICON_FOLDER,
    ICON_SONG,
    ICON_PLAYLIST,
    ICON_FAVORITE,
    ICON_SHUFFLE,
    ICON_REPEAT,
    ICON_DISPLAY,
    ICON_THEME,
    ICON_POWER,
    ICON_SLEEP,
    ICON_TIMEOUT,
    ICON_CONTROLS,
    ICON_START,
    ICON_RESCAN,
    ICON_DIAGNOSTICS,
    ICON_GAIN,
    ICON_JACK,
    ICON_USB,
    ICON_BLUETOOTH,
    ICON_KEY_PLUS,
    ICON_KEY_MINUS,
    ICON_KEY_PLAY,
    ICON_KEY_MENU,
    ICON_DISC,
    ICON_HUB,
    ICON_PLAY,
    ICON_PAUSE,
    ICON_NEXT,
    ICON_PREVIOUS,
    ICON_SEEK,
    ICON_QUEUE,
    ICON_SPEAKER,
    ICON_ALBUM_SMALL,
    ICON_SHUFFLE_SMALL,
    ICON_BOLT,
    ICON_BLUETOOTH_SMALL,
    ICON_LOCK,
    ICON_SONG_SMALL,
    ICON_COUNT
} IconId;

enum {
    ICON_LARGE_GRID = 20,
    ICON_SMALL_GRID = 14,
    ICON_NARROW_COLUMNS = 9
};

typedef struct IconInk {
    Color main;
    Color accent;
} IconInk;

int icon_rows(IconId icon);
int icon_width(IconId icon, int height);
void icon_draw(Canvas *canvas, IconId icon, Rect area, IconInk ink);
