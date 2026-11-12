#include "now_playing_screen.h"

#include "core/i18n.h"
#include "ui/header.h"
#include "ui/label.h"

static void load_text(NowPlayingScreen *now_playing, uint16_t track)
{
    const Library *library = now_playing->config.library;
    LibraryTrack record;
    LibraryGroup artist;
    now_playing->title[0] = '\0';
    now_playing->artist[0] = '\0';
    now_playing->has_text = false;
    now_playing->text_track = track;
    if (!library_read_track(library, track, &record)) {
        return;
    }
    library_read_string(library, record.title, now_playing->title, NOW_PLAYING_TEXT_CAPACITY);
    if (library_read_group(library, LIBRARY_ARTISTS, record.artist, &artist)) {
        library_read_string(library, artist.name, now_playing->artist, NOW_PLAYING_TEXT_CAPACITY);
    }
}

static void fallback_update(void *context, uint32_t now_ms)
{
    NowPlayingScreen *now_playing = context;
    PlayerStatus status = player_status(now_playing->config.player);
    now_playing->now_ms = now_ms;
    if (status.has_track && (now_playing->has_text || now_playing->text_track != status.track ||
                             !now_playing->status.has_track)) {
        load_text(now_playing, status.track);
    }
    now_playing->status = status;
}

static ScreenAction fallback_key(void *context, KeyEvent event, uint32_t now_ms)
{
    NowPlayingScreen *now_playing = context;
    (void)now_ms;
    if (event.gesture != KEY_GESTURE_PRESS) {
        return screen_action_none();
    }
    if (event.key == KEY_PLAY) {
        player_toggle_pause(now_playing->config.player);
    }
    return event.key == KEY_BACK ? screen_action_pop() : screen_action_none();
}

static void fallback_draw(void *context, Canvas *canvas)
{
    const NowPlayingScreen *now_playing = context;
    const Theme *theme = now_playing->config.theme;
    Rect content = theme_content_area(theme);
    int padding = theme->metrics.padding;
    int line = font_line_height(theme->fonts.body);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, i18n_text(STR_NOW_PLAYING));
    Rect row = {content.x + padding, content.y + padding, content.width - 2 * padding, line};
    bool playing = now_playing->status.state == PLAYER_PLAYING;
    const char *lines[] = {now_playing->status.has_track ? now_playing->title : "",
                           now_playing->status.has_track ? now_playing->artist : "",
                           i18n_text(playing ? STR_PLAYING : STR_PAUSED)};
    for (int index = 0; index < 3; index++) {
        Color color = index == 0 ? theme->colors.text : theme->colors.text_secondary;
        label_draw(canvas, (TextStyle){theme->fonts.body, color}, (Label){row, lines[index]});
        row.y += line + padding;
    }
}

void now_playing_screen_setup(NowPlayingScreen *now_playing, NowPlayingConfig config)
{
    now_playing->config = config;
    now_playing->destination = NULL;
    now_playing->flip_prompted = false;
    now_playing->has_text = false;
    now_playing->scrubbing = false;
    now_playing->scrub_position_ms = 0;
    now_playing->repeat_feedback_ms = 0;
    now_playing->feedback = NOW_PLAYING_FEEDBACK_NONE;
    now_playing->feedback_started_ms = 0;
    now_playing->status = (PlayerStatus){.state = PLAYER_IDLE};
    now_playing->screen = (Screen){
        .title = i18n_text(STR_NOW_PLAYING),
        .context = now_playing,
        .hold_volume_keys = true,
    };
    now_playing_fallback_attach(now_playing);
}

void now_playing_fallback_attach(NowPlayingScreen *now_playing)
{
    now_playing->scrubbing = false;
    now_playing->screen.enter = NULL;
    now_playing->screen.handle_key = fallback_key;
    now_playing->screen.update = fallback_update;
    now_playing->screen.draw = fallback_draw;
}
