#include "lyrics_screen.h"
#include "listening_work.h"

#include <stddef.h>

#include "core/format.h"
#include "core/i18n.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "library/lyrics.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/message_screen.h"
#include "ui/navigation.h"

enum {
    FOOTER_HEIGHT = 24,
    LINE_GAP = 6
};

static union {
    LyricsWork lyrics;
    uint32_t lent[LYRICS_LENT_SECTORS * 128];
} lyrics_work;
_Static_assert(sizeof(LyricsWork) >= LYRICS_LENT_SECTORS * 512, "the lent sectors fit the work");
static const void *lyrics_owner;
static LyricsWork *const work = &lyrics_work.lyrics;

uint32_t *lyrics_screen_lend_work(void)
{
    *work = (LyricsWork){.center = -2};
    lyrics_owner = NULL;
    return lyrics_work.lent;
}

static bool read_lyrics(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const HalFile *file = context;
    bool read = hal_file_read(*file, offset, bytes, length);
    hal_audio_service();
    hal_system_keep_alive();
    return read;
}

static bool sidecar_path(LyricsScreen *screen, uint16_t track)
{
    LibraryTrack record;
    screen->path[0] = '/';
    if (!library_read_track(screen->config.library, track, &record) ||
        !library_read_string(screen->config.library, record.path, screen->path + 1,
                             sizeof screen->path - 1)) {
        return false;
    }
    size_t end = 0;
    size_t dot = 0;
    while (screen->path[end] != '\0') {
        dot = screen->path[end] == '/' ? 0 : screen->path[end] == '.' ? end : dot;
        end++;
    }
    end = dot != 0 ? dot : end;
    if (end + 5 > sizeof screen->path) {
        return false;
    }
    static const char EXTENSION[] = ".lrc";
    for (size_t index = 0; index < sizeof EXTENSION; index++) {
        screen->path[end + index] = EXTENSION[index];
    }
    screen->cue_start_ms = record.start_ms;
    return true;
}

static bool open_sidecar(LyricsScreen *screen, HalFile *file)
{
    if (hal_file_open(file, screen->path)) {
        return true;
    }
    size_t length = 0;
    while (screen->path[length] != '\0') {
        length++;
    }
    if (length < 4) {
        return false;
    }
    screen->path[length - 3] = 'L';
    screen->path[length - 2] = 'R';
    screen->path[length - 1] = 'C';
    return hal_file_open(file, screen->path);
}

static void load(LyricsScreen *screen, PlayerStatus status)
{
    *work = (LyricsWork){.center = -2};
    lyrics_owner = screen;
    screen->known = true;
    screen->track = status.track;
    screen->token = screen->config.player->current_token;
    screen->scroll = 0;
    HalFile file;
    if (!status.has_track || !sidecar_path(screen, status.track) || !open_sidecar(screen, &file)) {
        return;
    }
    work->ready = lyrics_parse(&work->index, (ByteSource){read_lyrics, &file}, file.size);
    hal_file_close(&file);
}

static void cache_lines(LyricsScreen *screen, int center, bool timed)
{
    for (int index = 0; index < LYRICS_VISIBLE_LINES; index++) {
        work->lines[index][0] = '\0';
    }
    work->center = center;
    work->timed = timed;
    HalFile file;
    if (!open_sidecar(screen, &file)) {
        work->ready = false;
        return;
    }
    for (int index = 0; index < LYRICS_VISIBLE_LINES; index++) {
        int line = center + index - 1;
        if (line >= 0 && line < work->index.count) {
            if (!lyrics_text((ByteSource){read_lyrics, &file}, &work->index.lines[line],
                             work->lines[index], LYRICS_LINE_BYTES)) {
                work->ready = false;
                break;
            }
        }
    }
    hal_file_close(&file);
}

static void update(void *context, uint32_t now_ms)
{
    LyricsScreen *screen = context;
    PlayerStatus status = player_status(screen->config.player);
    (void)now_ms;
    if (lyrics_owner != screen || !screen->known || screen->track != status.track ||
        screen->token != screen->config.player->current_token) {
        load(screen, status);
    }
    if (!work->ready || work->index.count == 0) {
        return;
    }
    uint32_t position = status.position_ms;
    position =
        UINT32_MAX - position < screen->cue_start_ms ? UINT32_MAX : position + screen->cue_start_ms;
    int current = lyrics_current(&work->index, position);
    int center = (current < 0 ? 0 : current) + screen->scroll;
    center = center < 0 ? 0 : center >= work->index.count ? work->index.count - 1 : center;
    if (center != work->center || work->timed != (current == center)) {
        cache_lines(screen, center, current == center);
    }
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    LyricsScreen *screen = context;
    Navigation navigation = navigation_from_key(event);
    if (navigation.intent == NAVIGATION_HOME) {
        return screen_action_pop_to_root();
    }
    if (navigation.intent == NAVIGATION_BACK) {
        return screen_action_pop();
    }
    if (navigation.intent == NAVIGATION_PREVIOUS || navigation.intent == NAVIGATION_NEXT) {
        int scroll = screen->scroll + (navigation.intent == NAVIGATION_NEXT ? 1 : -1);
        if (scroll > -LYRICS_LINE_CAPACITY && scroll < LYRICS_LINE_CAPACITY) {
            screen->scroll = (int16_t)scroll;
        }
    } else if (navigation.intent == NAVIGATION_OPTIONS) {
        screen->scroll = 0;
    } else if (navigation.intent == NAVIGATION_SELECT) {
        player_toggle_pause(screen->config.player);
    }
    update(screen, now_ms);
    return screen_action_none();
}

static void draw_footer(LyricsScreen *screen, Canvas *canvas, Rect content)
{
    const Theme *theme = screen->config.theme;
    PlayerStatus status = player_status(screen->config.player);
    int padding = theme->metrics.padding;
    int top = content.y + content.height - FOOTER_HEIGHT;
    int width = content.width - 2 * padding;
    canvas_fill_rect(canvas, (Rect){content.x + padding, top, width, 3}, theme->colors.divider);
    uint32_t fill =
        status.duration_ms == 0
            ? 0
            : (uint32_t)((uint64_t)status.position_ms * (uint32_t)width / status.duration_ms);
    canvas_fill_rect(
        canvas, (Rect){content.x + padding, top, fill > (uint32_t)width ? width : (int)fill, 3},
        theme->colors.selection_top);
    char elapsed[16];
    format_duration(status.position_ms, elapsed, sizeof elapsed);
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    label_draw(canvas, style, (Label){{content.x + padding, top + 4, width, 20}, elapsed});
}

static void draw(void *context, Canvas *canvas)
{
    LyricsScreen *screen = context;
    const Theme *theme = screen->config.theme;
    Rect content = theme_content_area(theme);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, screen->screen.title);
    if (!work->ready || work->index.count == 0) {
        Rect message = content;
        message.height = content.height * 2 / 3;
        message_draw(canvas, theme, message, i18n_text(STR_NO_LYRICS));
        TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
        Rect hint = {content.x + theme->metrics.padding, content.y + message.height,
                     content.width - 2 * theme->metrics.padding, content.height - message.height};
        label_draw_wrapped(canvas, style, (Label){hint, i18n_text(STR_LYRICS_HINT)});
        return;
    }
    Rect area = content;
    area.x += theme->metrics.padding;
    area.width -= 2 * theme->metrics.padding;
    area.height -= FOOTER_HEIGHT;
    int side = (area.height - 2 * LINE_GAP) / 4;
    for (int index = 0; index < LYRICS_VISIBLE_LINES; index++) {
        bool current = index == 1;
        Rect line = {area.x,
                     area.y + (index == 0   ? 0
                               : index == 1 ? side + LINE_GAP
                                            : area.height - side),
                     area.width, current ? area.height - 2 * side - 2 * LINE_GAP : side};
        TextStyle style = {.font = current && work->timed ? theme->fonts.title : theme->fonts.body,
                           .color = current && work->timed ? theme->colors.text
                                                           : theme->colors.text_secondary};
        label_draw_wrapped(canvas, style, (Label){line, work->lines[index]});
    }
    draw_footer(screen, canvas, content);
}

static void enter(void *context, uint32_t now_ms)
{
    LyricsScreen *screen = context;
    screen->known = false;
    update(screen, now_ms);
}

Screen *lyrics_screen_init(LyricsScreen *screen, LyricsScreenConfig config)
{
    *screen = (LyricsScreen){.config = config};
    screen->screen = (Screen){.title = i18n_text(STR_LYRICS),
                              .context = screen,
                              .enter = enter,
                              .handle_key = handle_key,
                              .update = update,
                              .draw = draw};
    return &screen->screen;
}
