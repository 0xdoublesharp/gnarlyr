#include "now_playing_screen.h"

#include <stddef.h>

#include "board_layout.h"
#include "cassette_art.h"
#include "core/format.h"
#include "core/string_builder.h"
#include "gfx/image.h"
#include "settings/settings.h"
#include "ui/header.h"
#include "ui/icons.h"
#include "ui/label.h"
#include "ui/marquee.h"
#include "ui/message_screen.h"

enum {
    CODEC_PADDING = 4,
    CODEC_GAP = 7,
    OUTLINE = 1,
    TIME_CAPACITY = 16,
    TIME_SLOT = 60,
    TRACK_ALPHA = 6,
    PLAYHEAD_WIDTH = 3,
    PLAYHEAD_OVERHANG = 3,
    BADGE_WIDTH = 88,
    BADGE_HEIGHT = 62,
    BADGE_EDGE = 2,
    BADGE_ICON = 2 * ICON_SMALL_GRID,
    BADGE_ICON_TOP = 8,
    HINT_ICON = ICON_SMALL_GRID,
    HINT_ICON_GAP = 5,
    HINT_SPACING = 20,
    HINT_SLOT_COUNT = 8,
    FIRST_TAP_HINT = 5,
    SEEK_HINT_COUNT = 3,
    VOLUME_BAR_LEFT = 20,
    VOLUME_BAR_TOP = 7,
    VOLUME_BAR_WIDTH = 66,
    VOLUME_BAR_HEIGHT = 4,
    STATUS_CAPACITY = 64,
    BITRATE_CAPACITY = 24,
    RATE_CAPACITY = 16
};

static const NowPlayingLayout *layout(void)
{
    return &BOARD_LAYOUT.now_playing;
}

static const char SEPARATOR[] = " \xC2\xB7 ";

static const char NOTHING_PLAYING[] = "Nothing playing";
static const char *const SHUFFLE_TEXT[SHUFFLE_MODE_COUNT] = {"", "Shuffle Songs", "Shuffle Albums"};
static const char *const REPEAT_TEXT[REPEAT_MODE_COUNT] = {"", "Repeat One", "Repeat All"};
static const uint8_t HINT_SLOTS[HINT_SLOT_COUNT] = {
    PLAYBACK_VOLUME_DOWN_HOLD, PLAYBACK_VOLUME_UP_HOLD,  PLAYBACK_PLAY_HOLD,     PLAYBACK_MENU_TAP,
    PLAYBACK_MENU_HOLD,        PLAYBACK_VOLUME_DOWN_TAP, PLAYBACK_VOLUME_UP_TAP, PLAYBACK_PLAY_TAP,
};
static const char *const HINT_LABELS[HINT_SLOT_COUNT] = {
    "Hold \xE2\x80\x93", "Hold +", "Hold Play", "Menu", "Hold Menu", "\xE2\x80\x93", "+", "Play",
};
static const uint8_t ACTION_ICONS[BUTTON_ACTION_COUNT] = {
    ICON_NONE,     ICON_PLAY,    ICON_QUEUE,   ICON_SEEK,  ICON_NEXT,
    ICON_PREVIOUS, ICON_SPEAKER, ICON_SPEAKER, ICON_START,
};
static const NowPlayingHint SEEK_HINTS[SEEK_HINT_COUNT] = {
    {ICON_SEEK, "\xE2\x80\x93 / +"},
    {ICON_PLAY, "Play jumps"},
    {ICON_NONE, "Back cancels"},
};
static const uint8_t FEEDBACK_ICONS[] = {ICON_NONE,     ICON_SPEAKER, ICON_NEXT,
                                         ICON_PREVIOUS, ICON_PAUSE,   ICON_PLAY};
static const char *const FEEDBACK_TEXT[] = {"", "Volume ", "Next", "Previous", "Paused", "Playing"};

const char *now_playing_codec_name(HalAudioCodec codec)
{
    switch (codec) {
    case HAL_AUDIO_CODEC_FLAC:
        return "FLAC";
    case HAL_AUDIO_CODEC_MP3:
        return "MP3";
    case HAL_AUDIO_CODEC_WAV:
        return "WAV";
    case HAL_AUDIO_CODEC_AAC:
        return "AAC";
    case HAL_AUDIO_CODEC_ALAC:
        return "ALAC";
    case HAL_AUDIO_CODEC_UNKNOWN:
        return "";
    }
    return "";
}

void now_playing_stream_text(HalAudioFormat format, char *buffer, size_t capacity)
{
    StringBuilder builder = string_builder_make(buffer, capacity);
    if (format.codec == HAL_AUDIO_CODEC_UNKNOWN || format.sample_rate == 0) {
        return;
    }
    if (format.bits_per_sample > 0) {
        string_builder_append_unsigned(&builder, format.bits_per_sample);
        string_builder_append(&builder, "-bit");
        string_builder_append(&builder, SEPARATOR);
    }
    char rate[RATE_CAPACITY];
    format_sample_rate(format.sample_rate, rate, sizeof rate);
    string_builder_append(&builder, rate);
}

void now_playing_detail_text(const PlayerStatus *status, char *buffer, size_t capacity)
{
    StringBuilder builder = string_builder_make(buffer, capacity);
    bool has_bitrate =
        status->format.codec != HAL_AUDIO_CODEC_UNKNOWN && status->format.bitrate_kbps > 0;
    if (has_bitrate) {
        char bitrate[BITRATE_CAPACITY];
        format_bitrate(status->format.bitrate_kbps, bitrate, sizeof bitrate);
        string_builder_append(&builder, bitrate);
    }
    if (status->place.total == 0) {
        return;
    }
    if (has_bitrate) {
        string_builder_append(&builder, SEPARATOR);
    }
    string_builder_append_unsigned(&builder, status->place.number);
    string_builder_append(&builder, " of ");
    string_builder_append_unsigned(&builder, status->place.total);
}

static NowPlayingCommand by_gesture(KeyGesture gesture, NowPlayingCommand press,
                                    NowPlayingCommand hold)
{
    switch (gesture) {
    case KEY_GESTURE_PRESS:
        return press;
    case KEY_GESTURE_LONG_PRESS:
        return hold;
    case KEY_GESTURE_REPEAT:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

uint32_t now_playing_scrub_target(uint32_t position_ms, uint32_t duration_ms, int32_t delta_ms)
{
    int64_t target = (int64_t)position_ms + delta_ms;
    if (target < 0) {
        return 0;
    }
    return target > duration_ms ? duration_ms : (uint32_t)target;
}

static NowPlayingCommand scrub_command_for(KeyEvent event)
{
    bool held = event.gesture != KEY_GESTURE_PRESS;
    switch (event.key) {
    case KEY_VOLUME_UP:
        return held ? NOW_PLAYING_SCRUB_FORWARD_FAST : NOW_PLAYING_SCRUB_FORWARD;
    case KEY_VOLUME_DOWN:
        return held ? NOW_PLAYING_SCRUB_BACK_FAST : NOW_PLAYING_SCRUB_BACK;
    case KEY_PLAY:
        return by_gesture(event.gesture, NOW_PLAYING_SCRUB_COMMIT, NOW_PLAYING_NONE);
    case KEY_BACK:
        return by_gesture(event.gesture, NOW_PLAYING_SCRUB_CANCEL, NOW_PLAYING_HOME);
    case KEY_MENU:
    case KEY_POWER:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

NowPlayingCommand now_playing_command_for(KeyEvent event, bool scrubbing)
{
    if (scrubbing) {
        return scrub_command_for(event);
    }
    switch (event.key) {
    case KEY_VOLUME_UP:
        return by_gesture(event.gesture, NOW_PLAYING_VOLUME_UP, NOW_PLAYING_NEXT);
    case KEY_VOLUME_DOWN:
        return by_gesture(event.gesture, NOW_PLAYING_VOLUME_DOWN, NOW_PLAYING_PREVIOUS);
    case KEY_PLAY:
        return by_gesture(event.gesture, NOW_PLAYING_TOGGLE_PAUSE, NOW_PLAYING_SCRUB_START);
    case KEY_BACK:
        return by_gesture(event.gesture, NOW_PLAYING_LEAVE, NOW_PLAYING_HOME);
    case KEY_MENU:
        return by_gesture(event.gesture, NOW_PLAYING_UP_NEXT, NOW_PLAYING_NONE);
    case KEY_POWER:
        return NOW_PLAYING_NONE;
    }
    return NOW_PLAYING_NONE;
}

_Static_assert(PLAYBACK_BUTTON_COUNT == 2 * (KEY_MENU + 1), "playback slots follow media keys");

NowPlayingCommand now_playing_configured_command_for(KeyEvent event, bool scrubbing,
                                                     const Settings *settings)
{
    static const NowPlayingCommand commands[BUTTON_ACTION_COUNT] = {
        NOW_PLAYING_NONE,        NOW_PLAYING_TOGGLE_PAUSE, NOW_PLAYING_UP_NEXT,
        NOW_PLAYING_SCRUB_START, NOW_PLAYING_NEXT,         NOW_PLAYING_PREVIOUS,
        NOW_PLAYING_VOLUME_UP,   NOW_PLAYING_VOLUME_DOWN,  NOW_PLAYING_HOME,
    };
    if (settings == NULL || scrubbing || (unsigned)event.key > KEY_MENU) {
        return now_playing_command_for(event, scrubbing);
    }
    if ((unsigned)event.gesture > KEY_GESTURE_REPEAT) {
        return NOW_PLAYING_NONE;
    }
    bool held = event.gesture != KEY_GESTURE_PRESS;
    unsigned slot = (unsigned)event.key * 2 + (held ? 1u : 0u);
    uint8_t action = settings->button_actions[slot];
    if (event.gesture == KEY_GESTURE_REPEAT && action != BUTTON_ACTION_VOLUME_UP &&
        action != BUTTON_ACTION_VOLUME_DOWN) {
        return NOW_PLAYING_NONE;
    }
    return action < BUTTON_ACTION_COUNT ? commands[action] : NOW_PLAYING_NONE;
}

int now_playing_progress_width(uint32_t position_ms, uint32_t duration_ms, int width)
{
    if (duration_ms == 0) {
        return 0;
    }
    if (position_ms >= duration_ms) {
        return width;
    }
    return (int)((uint64_t)position_ms * (uint64_t)width / duration_ms);
}

NowPlayingFeedback now_playing_feedback_for(NowPlayingCommand command, PlayerState after)
{
    switch (command) {
    case NOW_PLAYING_VOLUME_UP:
    case NOW_PLAYING_VOLUME_DOWN:
        return NOW_PLAYING_FEEDBACK_VOLUME;
    case NOW_PLAYING_NEXT:
        return NOW_PLAYING_FEEDBACK_NEXT;
    case NOW_PLAYING_PREVIOUS:
        return NOW_PLAYING_FEEDBACK_PREVIOUS;
    case NOW_PLAYING_TOGGLE_PAUSE:
        return after == PLAYER_PAUSED ? NOW_PLAYING_FEEDBACK_PAUSE : NOW_PLAYING_FEEDBACK_PLAY;
    default:
        return NOW_PLAYING_FEEDBACK_NONE;
    }
}

bool now_playing_feedback_visible(NowPlayingFeedback feedback, uint32_t started_ms, uint32_t now_ms)
{
    if (feedback == NOW_PLAYING_FEEDBACK_NONE) {
        return false;
    }
    uint32_t duration = feedback == NOW_PLAYING_FEEDBACK_VOLUME ? NOW_PLAYING_VOLUME_FEEDBACK_MS
                                                                : NOW_PLAYING_BADGE_MS;
    return now_ms - started_ms < duration;
}

int now_playing_volume_width(uint8_t volume, int width)
{
    uint8_t level = volume < SETTINGS_VOLUME_MAX ? volume : SETTINGS_VOLUME_MAX;
    return level * width / SETTINGS_VOLUME_MAX;
}

static void move_scrub(NowPlayingScreen *now_playing, int32_t delta_ms)
{
    now_playing->scrub_position_ms = now_playing_scrub_target(
        now_playing->scrub_position_ms, now_playing->status.duration_ms, delta_ms);
}

static void start_scrub(NowPlayingScreen *now_playing)
{
    now_playing->scrubbing = now_playing->status.has_track;
    now_playing->scrub_position_ms = now_playing->status.position_ms;
}

static void commit_scrub(NowPlayingScreen *now_playing)
{
    player_seek(now_playing->config.player, now_playing->scrub_position_ms);
    now_playing->scrubbing = false;
}

static ScreenAction run_scrub_command(NowPlayingScreen *now_playing, NowPlayingCommand command)
{
    switch (command) {
    case NOW_PLAYING_SCRUB_START:
        start_scrub(now_playing);
        break;
    case NOW_PLAYING_SCRUB_FORWARD:
        move_scrub(now_playing, NOW_PLAYING_SCRUB_STEP_MS);
        break;
    case NOW_PLAYING_SCRUB_FORWARD_FAST:
        move_scrub(now_playing, NOW_PLAYING_SCRUB_FAST_STEP_MS);
        break;
    case NOW_PLAYING_SCRUB_BACK:
        move_scrub(now_playing, -NOW_PLAYING_SCRUB_STEP_MS);
        break;
    case NOW_PLAYING_SCRUB_BACK_FAST:
        move_scrub(now_playing, -NOW_PLAYING_SCRUB_FAST_STEP_MS);
        break;
    case NOW_PLAYING_SCRUB_COMMIT:
        commit_scrub(now_playing);
        break;
    default:
        now_playing->scrubbing = false;
        break;
    }
    return screen_action_none();
}

static ScreenAction run_command(NowPlayingScreen *now_playing, NowPlayingCommand command)
{
    Player *player = now_playing->config.player;
    switch (command) {
    case NOW_PLAYING_TOGGLE_PAUSE:
        player_toggle_pause(player);
        break;
    case NOW_PLAYING_NEXT:
        player_skip(player, 1);
        break;
    case NOW_PLAYING_PREVIOUS:
        player_skip(player, -1);
        break;
    case NOW_PLAYING_VOLUME_UP:
        player_change_volume(player, 1);
        break;
    case NOW_PLAYING_VOLUME_DOWN:
        player_change_volume(player, -1);
        break;
    case NOW_PLAYING_LEAVE:
        return screen_action_pop();
    case NOW_PLAYING_UP_NEXT:
        return now_playing->config.up_next != NULL ? screen_action_push(now_playing->config.up_next)
                                                   : screen_action_none();
    case NOW_PLAYING_HOME:
        now_playing->scrubbing = false;
        return screen_action_pop_to_root();
    case NOW_PLAYING_SCRUB_START:
    case NOW_PLAYING_SCRUB_FORWARD:
    case NOW_PLAYING_SCRUB_FORWARD_FAST:
    case NOW_PLAYING_SCRUB_BACK:
    case NOW_PLAYING_SCRUB_BACK_FAST:
    case NOW_PLAYING_SCRUB_COMMIT:
    case NOW_PLAYING_SCRUB_CANCEL:
        return run_scrub_command(now_playing, command);
    case NOW_PLAYING_NONE:
        break;
    }
    return screen_action_none();
}

static void load_texts(NowPlayingScreen *now_playing, uint16_t track_index)
{
    const Library *library = now_playing->config.library;
    LibraryTrack track;
    LibraryAlbum album;
    LibraryGroup artist;
    now_playing->title[0] = '\0';
    now_playing->artist[0] = '\0';
    now_playing->album[0] = '\0';
    if (!library_read_track(library, track_index, &track)) {
        return;
    }
    library_read_string(library, track.title, now_playing->title, NOW_PLAYING_TEXT_CAPACITY);
    if (library_read_group(library, LIBRARY_ARTISTS, track.artist, &artist)) {
        library_read_string(library, artist.name, now_playing->artist, NOW_PLAYING_TEXT_CAPACITY);
    }
    now_playing->album_index = track.album;
    if (library_read_album(library, track.album, &album)) {
        library_read_string(library, album.title, now_playing->album, NOW_PLAYING_TEXT_CAPACITY);
    }
}

static void refresh(NowPlayingScreen *now_playing, uint32_t now_ms)
{
    now_playing->now_ms = now_ms;
    now_playing->status = player_status(now_playing->config.player);
    bool changed = !now_playing->has_text || now_playing->text_track != now_playing->status.track;
    if (now_playing->status.has_track && changed) {
        load_texts(now_playing, now_playing->status.track);
        now_playing->has_text = true;
        now_playing->text_track = now_playing->status.track;
        now_playing->text_changed_ms = now_ms;
    }
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    NowPlayingScreen *now_playing = context;
    NowPlayingCommand command = now_playing_configured_command_for(event, now_playing->scrubbing,
                                                                   now_playing->config.settings);
    ScreenAction action = run_command(now_playing, command);
    refresh(now_playing, now_ms);
    NowPlayingFeedback feedback = now_playing_feedback_for(command, now_playing->status.state);
    if (feedback != NOW_PLAYING_FEEDBACK_NONE) {
        now_playing->feedback = feedback;
        now_playing->feedback_started_ms = now_ms;
    }
    return action;
}

static void enter(void *context, uint32_t now_ms)
{
    NowPlayingScreen *now_playing = context;
    now_playing->has_text = false;
    now_playing->scrubbing = false;
    now_playing->feedback = NOW_PLAYING_FEEDBACK_NONE;
    refresh(now_playing, now_ms);
}

static bool showing(const NowPlayingScreen *now_playing, NowPlayingFeedback feedback)
{
    return now_playing->feedback == feedback &&
           now_playing_feedback_visible(feedback, now_playing->feedback_started_ms,
                                        now_playing->now_ms);
}

static void update(void *context, uint32_t now_ms)
{
    NowPlayingScreen *now_playing = context;
    refresh(now_playing, now_ms);
    if (now_playing->config.covers != NULL && now_playing->status.has_track) {
        cover_cache_load_full(now_playing->config.covers, now_playing->album_index);
    }
}

static bool current_cover(const NowPlayingScreen *now_playing, Image *image)
{
    const CoverCache *covers = now_playing->config.covers;
    return covers != NULL && now_playing->has_text &&
           cover_cache_full(covers, now_playing->album_index, image) == COVER_LOADED;
}

static int text_left(const NowPlayingScreen *now_playing)
{
    int padding = now_playing->config.theme->metrics.padding;
    return padding + COVER_FULL_SIZE + layout()->cover_gap;
}

static Rect column_area(const NowPlayingScreen *now_playing, int top, int height)
{
    const Theme *theme = now_playing->config.theme;
    int left = text_left(now_playing);
    return (Rect){left, top, theme->metrics.screen_width - left - theme->metrics.padding, height};
}

static uint32_t shown_position(const NowPlayingScreen *now_playing)
{
    return now_playing->scrubbing ? now_playing->scrub_position_ms
                                  : now_playing->status.position_ms;
}

static void draw_cover(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    int left = theme->metrics.padding;
    int top = layout()->cover_top;
    Image cover;
    if (!current_cover(now_playing, &cover)) {
        CassetteTape tape = {shown_position(now_playing), now_playing->status.duration_ms,
                             now_playing->album};
        Rect cassette = {left, top + (COVER_FULL_SIZE - CASSETTE_HEIGHT) / 2, CASSETTE_WIDTH,
                         CASSETTE_HEIGHT};
        cassette_art_draw(canvas, cassette, theme, tape);
        return;
    }
    canvas_fill_rect(canvas, (Rect){left - 2, top - 2, COVER_FULL_SIZE + 4, COVER_FULL_SIZE + 4},
                     theme->colors.divider);
    image_draw_quad(canvas, cover,
                    (Quad){left, COVER_FULL_SIZE, top, COVER_FULL_SIZE, top, COVER_FULL_SIZE});
}

static Rect line_area(const Theme *theme, int top, int height)
{
    int padding = theme->metrics.padding;
    return (Rect){padding, top, theme->metrics.screen_width - 2 * padding, height};
}

static void draw_text_line(const NowPlayingScreen *now_playing, Canvas *canvas, Label label,
                           TextStyle style)
{
    int width = text_width(style.font, label.text);
    if (width <= label.area.width) {
        label_draw(canvas, style, label);
        return;
    }
    MarqueeTiming timing = now_playing->config.theme->marquee;
    uint32_t elapsed = now_playing->now_ms - now_playing->text_changed_ms;
    int offset = marquee_offset(timing, width, label.area.width, elapsed);
    label_draw_scrolling(canvas, style, label, (Marquee){.offset = offset, .gap = timing.gap_px});
}

static void draw_track_text(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    TextStyle title = {.font = theme->fonts.title, .color = theme->colors.text};
    TextStyle artist = {.font = theme->fonts.body, .color = theme->colors.text_secondary};
    TextStyle album = {.font = theme->fonts.body, .color = theme->colors.text_tertiary};
    draw_text_line(
        now_playing, canvas,
        (Label){column_area(now_playing, layout()->title_top, layout()->text_line_height),
                now_playing->title},
        title);
    draw_text_line(
        now_playing, canvas,
        (Label){column_area(now_playing, layout()->artist_top, layout()->text_line_height),
                now_playing->artist},
        artist);
    draw_text_line(
        now_playing, canvas,
        (Label){column_area(now_playing, layout()->album_top, layout()->text_line_height),
                now_playing->album},
        album);
}

static void draw_right_aligned(Canvas *canvas, TextStyle style, Rect area, const char *text)
{
    int width = text_width(style.font, text);
    Rect right = {area.x + area.width - width, area.y, width, area.height};
    label_draw(canvas, style, (Label){.area = right, .text = text});
}

static void draw_progress(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    const ThemeColors *colors = &theme->colors;
    int inset = theme->metrics.padding + TIME_SLOT;
    Rect bar = {inset, layout()->progress_top, theme->metrics.screen_width - 2 * inset,
                layout()->progress_height};
    int filled = now_playing_progress_width(shown_position(now_playing),
                                            now_playing->status.duration_ms, bar.width);
    Color track = color_blend(colors->header_background, colors->header_text, TRACK_ALPHA);
    Color head = now_playing->scrubbing ? colors->selection_background : colors->header_text;
    canvas_fill_rect(canvas, (Rect){bar.x, bar.y + bar.height / 2 - 1, bar.width, 2}, track);
    canvas_fill_rect(canvas, (Rect){bar.x, bar.y, filled, bar.height}, colors->selection_shine);
    canvas_fill_rect(canvas,
                     (Rect){bar.x + filled - 1, bar.y - PLAYHEAD_OVERHANG, PLAYHEAD_WIDTH,
                            bar.height + 2 * PLAYHEAD_OVERHANG},
                     head);
}

static void draw_times(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    uint32_t position_ms = shown_position(now_playing);
    uint32_t duration_ms = now_playing->status.duration_ms;
    uint32_t remaining_ms = duration_ms > position_ms ? duration_ms - position_ms : 0;
    char elapsed[TIME_CAPACITY];
    char remaining[TIME_CAPACITY] = "-";
    format_duration(position_ms, elapsed, sizeof elapsed);
    format_duration(remaining_ms, remaining + 1, sizeof remaining - 1);
    Rect area = line_area(theme, layout()->times_top, layout()->small_line_height);
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.header_text};
    label_draw(canvas, style, (Label){.area = area, .text = elapsed});
    draw_right_aligned(canvas, style, area, remaining);
}

int now_playing_hints(const Settings *settings, bool scrubbing, NowPlayingHint *hints)
{
    if (scrubbing) {
        for (int index = 0; index < SEEK_HINT_COUNT; index++) {
            hints[index] = SEEK_HINTS[index];
        }
        return SEEK_HINT_COUNT;
    }
    int count = 0;
    for (int index = 0; index < HINT_SLOT_COUNT && count < NOW_PLAYING_HINT_CAPACITY; index++) {
        PlaybackButton slot = (PlaybackButton)HINT_SLOTS[index];
        unsigned fallback = settings_default_button_action(slot);
        unsigned action = settings != NULL ? settings->button_actions[slot] : fallback;
        bool expected = index >= FIRST_TAP_HINT && action == fallback;
        if (action != BUTTON_ACTION_NONE && action < BUTTON_ACTION_COUNT && !expected) {
            hints[count++] = (NowPlayingHint){ACTION_ICONS[action], HINT_LABELS[index]};
        }
    }
    return count;
}

static int hint_width(const Font *font, NowPlayingHint hint)
{
    int icon = hint.icon != ICON_NONE ? HINT_ICON + HINT_ICON_GAP : 0;
    return icon + text_width(font, hint.label);
}

static void draw_legend(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    NowPlayingHint hints[NOW_PLAYING_HINT_CAPACITY];
    int count = now_playing_hints(now_playing->config.settings, now_playing->scrubbing, hints);
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.header_text};
    IconInk ink = {theme->colors.selection_background, theme->colors.selection_background};
    int total = -HINT_SPACING;
    for (int index = 0; index < count; index++) {
        total += hint_width(style.font, hints[index]) + HINT_SPACING;
    }
    int x = (theme->metrics.screen_width - total) / 2;
    int top = layout()->legend_top;
    for (int index = 0; index < count; index++) {
        if (hints[index].icon != ICON_NONE) {
            icon_draw(canvas, (IconId)hints[index].icon, (Rect){x, top + 2, HINT_ICON, HINT_ICON},
                      ink);
            x += HINT_ICON + HINT_ICON_GAP;
        }
        int width = text_width(style.font, hints[index].label);
        Rect area = {x, top, width, layout()->small_line_height};
        label_draw(canvas, style, (Label){area, hints[index].label});
        x += width + HINT_SPACING;
    }
}

static void draw_deck(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    int top = layout()->deck_top;
    int width = theme->metrics.screen_width;
    canvas_fill_gradient(canvas, (Rect){0, top, width, theme->metrics.screen_height - top},
                         theme->colors.header_top, theme->colors.header_background);
    canvas_fill_rect(canvas, (Rect){0, top, width, 1}, theme->colors.divider);
    draw_times(now_playing, canvas);
    draw_progress(now_playing, canvas);
    draw_legend(now_playing, canvas);
}

static void draw_volume(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    const ThemeColors *colors = &theme->colors;
    int left = theme->metrics.padding;
    int top = layout()->volume_top;
    icon_draw(canvas, ICON_SPEAKER, (Rect){left, top + 2, HINT_ICON, HINT_ICON},
              (IconInk){colors->text_secondary, colors->accent});
    Rect bar = {left + VOLUME_BAR_LEFT, top + VOLUME_BAR_TOP, VOLUME_BAR_WIDTH, VOLUME_BAR_HEIGHT};
    canvas_fill_rect(canvas, bar, colors->divider);
    bar.width = now_playing_volume_width(now_playing->status.volume, VOLUME_BAR_WIDTH);
    canvas_fill_rect(canvas, bar, colors->accent);
    char number[TIME_CAPACITY];
    StringBuilder builder = string_builder_make(number, sizeof number);
    string_builder_append_unsigned(&builder, now_playing->status.volume);
    bool active = showing(now_playing, NOW_PLAYING_FEEDBACK_VOLUME);
    TextStyle style = {.font = theme->fonts.small,
                       .color = active ? colors->accent : colors->text_secondary};
    draw_right_aligned(canvas, style,
                       (Rect){left, top, COVER_FULL_SIZE, layout()->small_line_height}, number);
}

static void modes_text(const PlayerStatus *status, char *buffer, size_t capacity)
{
    StringBuilder builder = string_builder_make(buffer, capacity);
    string_builder_append(&builder, SHUFFLE_TEXT[status->shuffle]);
    if (status->shuffle != SHUFFLE_OFF && status->repeat != REPEAT_OFF) {
        string_builder_append(&builder, SEPARATOR);
    }
    string_builder_append(&builder, REPEAT_TEXT[status->repeat]);
}

static void draw_format(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    HalAudioFormat format = now_playing->status.format;
    const char *codec = now_playing_codec_name(format.codec);
    if (codec[0] == '\0') {
        return;
    }
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    Rect row = column_area(now_playing, layout()->format_top, layout()->small_line_height);
    int codec_width = text_width(style.font, codec) + 2 * CODEC_PADDING;
    Rect badge = {row.x, row.y, codec_width, row.height};
    canvas_fill_rect(canvas, badge, theme->colors.text_secondary);
    canvas_fill_rect(canvas,
                     (Rect){badge.x + OUTLINE, badge.y + OUTLINE, badge.width - 2 * OUTLINE,
                            badge.height - 2 * OUTLINE},
                     theme->colors.background);
    Rect codec_text = label_centered_area(style.font, badge, codec);
    label_draw(canvas, style, (Label){.area = codec_text, .text = codec});
    char stream[STATUS_CAPACITY];
    now_playing_stream_text(format, stream, sizeof stream);
    int stream_left = badge.x + badge.width + CODEC_GAP;
    Rect stream_area = {stream_left, row.y, row.x + row.width - stream_left, row.height};
    label_draw(canvas, style, (Label){.area = stream_area, .text = stream});
}

static void draw_details(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_tertiary};
    char detail[STATUS_CAPACITY];
    now_playing_detail_text(&now_playing->status, detail, sizeof detail);
    label_draw(canvas, style,
               (Label){column_area(now_playing, layout()->detail_top, layout()->small_line_height),
                       detail});
    char modes[STATUS_CAPACITY];
    modes_text(&now_playing->status, modes, sizeof modes);
    label_draw(
        canvas, style,
        (Label){column_area(now_playing, layout()->modes_top, layout()->small_line_height), modes});
}

static void draw_badge(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    NowPlayingFeedback feedback = now_playing->feedback;
    if (!showing(now_playing, feedback)) {
        return;
    }
    const Theme *theme = now_playing->config.theme;
    const ThemeColors *colors = &theme->colors;
    int center_x = theme->metrics.padding + COVER_FULL_SIZE / 2;
    int center_y = layout()->cover_top + COVER_FULL_SIZE / 2;
    Rect badge = {center_x - BADGE_WIDTH / 2, center_y - BADGE_HEIGHT / 2, BADGE_WIDTH,
                  BADGE_HEIGHT};
    canvas_fill_rect(canvas, badge, colors->selection_background);
    canvas_fill_rect(canvas,
                     (Rect){badge.x + BADGE_EDGE, badge.y + BADGE_EDGE,
                            badge.width - 2 * BADGE_EDGE, badge.height - 2 * BADGE_EDGE},
                     colors->header_background);
    Rect icon = {center_x - BADGE_ICON / 2, badge.y + BADGE_ICON_TOP, BADGE_ICON, BADGE_ICON};
    icon_draw(canvas, (IconId)FEEDBACK_ICONS[feedback], icon,
              (IconInk){colors->header_text, colors->selection_background});
    char caption[STATUS_CAPACITY];
    StringBuilder builder = string_builder_make(caption, sizeof caption);
    string_builder_append(&builder, FEEDBACK_TEXT[feedback]);
    if (feedback == NOW_PLAYING_FEEDBACK_VOLUME) {
        string_builder_append_unsigned(&builder, now_playing->status.volume);
    }
    TextStyle style = {.font = theme->fonts.small, .color = colors->header_text};
    Rect row = {badge.x, icon.y + BADGE_ICON + 2, badge.width, layout()->small_line_height};
    label_draw(canvas, style, (Label){label_centered_area(style.font, row, caption), caption});
}

static void draw(void *context, Canvas *canvas)
{
    const NowPlayingScreen *now_playing = context;
    const Theme *theme = now_playing->config.theme;
    Rect content = theme_content_area(theme);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, now_playing->screen.title);
    if (!now_playing->status.has_track) {
        message_draw_card(canvas, theme, content, (MessageCard){ICON_NOW_PLAYING, NOTHING_PLAYING});
        return;
    }
    draw_cover(now_playing, canvas);
    draw_volume(now_playing, canvas);
    draw_badge(now_playing, canvas);
    draw_track_text(now_playing, canvas);
    draw_format(now_playing, canvas);
    draw_details(now_playing, canvas);
    draw_deck(now_playing, canvas);
}

Screen *now_playing_screen_init(NowPlayingScreen *now_playing, NowPlayingConfig config)
{
    now_playing->config = config;
    now_playing->has_text = false;
    now_playing->scrubbing = false;
    now_playing->scrub_position_ms = 0;
    now_playing->feedback = NOW_PLAYING_FEEDBACK_NONE;
    now_playing->feedback_started_ms = 0;
    now_playing->status = (PlayerStatus){.state = PLAYER_IDLE};
    now_playing->screen = (Screen){
        .title = "Now Playing",
        .context = now_playing,
        .hold_volume_keys = true,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &now_playing->screen;
}
