#include "now_playing_screen.h"

#include <stddef.h>

#include "board_layout.h"
#include "cassette_art.h"
#include "core/format.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "gfx/image.h"
#include "home_art.h"
#include "neon_art.h"
#include "settings/settings.h"
#include "ui/header.h"
#include "ui/icons.h"
#include "ui/label.h"
#include "ui/marquee.h"
#include "ui/message_screen.h"

enum {
    MS_PER_SECOND = 1000,
    ART_SPINE = 3,
    ART_SHADOW = 5,
    TRANSPORT_ICON = 12,
    TIME_CAPACITY = 16,
    BADGE_SIZE = 40,
    BADGE_ICON_SIZE = 18,
    STATUS_CAPACITY = 64,
    BITRATE_CAPACITY = 24,
    RATE_CAPACITY = 16
};

static const NowPlayingLayout *layout(const NowPlayingScreen *now_playing)
{
    return board_now_playing_layout(now_playing->config.theme);
}

static const char SEPARATOR[] = " \xC2\xB7 ";

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
    case HAL_AUDIO_CODEC_VORBIS:
        return i18n_text(STR_CODEC_VORBIS);
    case HAL_AUDIO_CODEC_AIFF:
        return i18n_text(STR_CODEC_AIFF);
    case HAL_AUDIO_CODEC_DSD:
        return i18n_text(STR_CODEC_DSD);
    case HAL_AUDIO_CODEC_WMA:
        return i18n_text(STR_CODEC_WMA);
    case HAL_AUDIO_CODEC_APE:
        return i18n_text(STR_CODEC_APE);
    case HAL_AUDIO_CODEC_OPUS:
        return i18n_text(STR_CODEC_OPUS);
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
        i18n_format_counts(&builder, STR_BIT_DEPTH, format.bits_per_sample, 0);
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
    i18n_format_counts(&builder, STR_PLACE, status->place.number, status->place.total);
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
    case NOW_PLAYING_REPEAT_MARK:
        player_repeat_mark(player);
        now_playing->scrubbing = false;
        now_playing->repeat_feedback_ms = now_playing->now_ms;
        break;
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
    case NOW_PLAYING_SEEK_FORWARD:
        now_playing_seek_by(player, NOW_PLAYING_SCRUB_STEP_MS);
        break;
    case NOW_PLAYING_SEEK_FORWARD_FAST:
        now_playing_seek_by(player, NOW_PLAYING_SCRUB_FAST_STEP_MS);
        break;
    case NOW_PLAYING_SEEK_BACK:
        now_playing_seek_by(player, -NOW_PLAYING_SCRUB_STEP_MS);
        break;
    case NOW_PLAYING_SEEK_BACK_FAST:
        now_playing_seek_by(player, -NOW_PLAYING_SCRUB_FAST_STEP_MS);
        break;
    case NOW_PLAYING_LEAVE:
        return screen_action_pop();
    case NOW_PLAYING_QUICK_SETTINGS:
        return now_playing->config.quick_settings != NULL
                   ? screen_action_push(now_playing->config.quick_settings)
                   : screen_action_none();
    case NOW_PLAYING_LYRICS:
        return now_playing->config.lyrics != NULL ? screen_action_push(now_playing->config.lyrics)
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

static NowPlayingKeys keys_of(const NowPlayingScreen *now_playing)
{
    bool volume_shown =
        now_playing->feedback == NOW_PLAYING_FEEDBACK_VOLUME &&
        now_playing_adjusting_volume(now_playing->feedback_started_ms, now_playing->now_ms);
    return (NowPlayingKeys){.scrubbing = now_playing->scrubbing,
                            .adjusting_volume = volume_shown,
                            .skip = now_playing->config.settings->skip_gesture};
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    NowPlayingScreen *now_playing = context;
    now_playing->now_ms = now_ms;
    NowPlayingCommand command = now_playing_command_for(event, keys_of(now_playing));
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
    now_playing->destination = NULL;
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
    if (!now_playing->config.player->cassette.flip) {
        now_playing->flip_prompted = false;
    } else if (!now_playing->flip_prompted && now_playing->config.flip != NULL) {
        now_playing->flip_prompted = true;
        now_playing->destination = now_playing->config.flip;
    }
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

static int cover_left(const NowPlayingScreen *now_playing)
{
    const Theme *theme = now_playing->config.theme;
    return theme_is_portrait(theme)
               ? (theme->metrics.screen_width - layout(now_playing)->cover_size) / 2
               : theme->metrics.padding;
}

static int text_left(const NowPlayingScreen *now_playing)
{
    int padding = now_playing->config.theme->metrics.padding;
    return !theme_is_portrait(now_playing->config.theme)
               ? padding + layout(now_playing)->cover_size + layout(now_playing)->cover_gap
               : padding;
}

static Rect column_area(const NowPlayingScreen *now_playing, int top, int height)
{
    const Theme *theme = now_playing->config.theme;
    int left = text_left(now_playing);
    return (Rect){left, top, theme->metrics.screen_width - left - theme->metrics.padding, height};
}

static uint32_t shown_position(const NowPlayingScreen *now_playing)
{
    if (now_playing->scrubbing) {
        return now_playing->scrub_position_ms;
    }
    const PlayerStatus *status = &now_playing->status;
    return status->state == PLAYER_PLAYING && status->position_ms < status->duration_ms
               ? status->position_ms / MS_PER_SECOND * MS_PER_SECOND
               : status->position_ms;
}

static void draw_cover(const NowPlayingScreen *now_playing, Canvas *canvas, bool artwork)
{
    Image cover;
    const Theme *theme = now_playing->config.theme;
    int left = cover_left(now_playing);
    int top = layout(now_playing)->cover_top;
    int size = layout(now_playing)->cover_size;
    if (theme->horizon) {
        Rect stage = {0, theme->metrics.header_height,
                      theme_is_portrait(theme) ? theme->metrics.screen_width
                                               : left + size + theme->metrics.padding,
                      top + size + 10 - theme->metrics.header_height};
        if (!artwork) {
            home_art_draw_backdrop(canvas, stage, theme);
        }
        neon_art_frame(canvas, (Rect){left - 4, top - 4, size + 8, size + 8},
                       neon_art_ink(theme, theme->colors.accent));
    }
    canvas_fill_rect(canvas, (Rect){left + ART_SHADOW, top + ART_SHADOW, size, size},
                     theme->colors.backdrop_bottom);
    canvas_fill_rect(canvas, (Rect){left - ART_SPINE, top, ART_SPINE, size},
                     theme->colors.accent_alt);
    if (current_cover(now_playing, &cover)) {
        image_draw_quad(canvas, cover,
                        (Quad){left, size, top, size, top + ART_SPINE, size - ART_SPINE});
        return;
    }
    canvas_fill_rect(canvas, (Rect){left, top, size, size}, theme->colors.backdrop_top);
    cassette_art_draw(canvas,
                      (Rect){left + (size - CASSETTE_WIDTH) / 2, top + (size - CASSETTE_HEIGHT) / 2,
                             CASSETTE_WIDTH, CASSETTE_HEIGHT},
                      theme,
                      (CassetteTape){shown_position(now_playing), now_playing->status.duration_ms,
                                     FIRMWARE_SHORT_NAME});
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

static void draw_title(const NowPlayingScreen *now_playing, Canvas *canvas, TextStyle style)
{
    Label title = {column_area(now_playing, layout(now_playing)->title_top,
                               layout(now_playing)->text_line_height),
                   now_playing->title};
    if (theme_is_portrait(now_playing->config.theme) &&
        label_wrap_count(style.font, title.text, title.area.width) <= 2) {
        title.area.height *= 2;
        label_draw_paragraph(canvas, style, title);
        return;
    }
    draw_text_line(now_playing, canvas, title, style);
}

static void draw_track_text(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    TextStyle title = {.font = theme->fonts.title, .color = theme->colors.text};
    TextStyle artist = {.font = theme->fonts.body, .color = theme->colors.accent};
    TextStyle album = {.font = theme->fonts.small, .color = theme->colors.text_tertiary};
    draw_title(now_playing, canvas, title);
    draw_text_line(now_playing, canvas,
                   (Label){column_area(now_playing, layout(now_playing)->artist_top,
                                       layout(now_playing)->text_line_height),
                           now_playing->artist},
                   artist);
    draw_text_line(now_playing, canvas,
                   (Label){column_area(now_playing, layout(now_playing)->album_top,
                                       layout(now_playing)->small_line_height),
                           now_playing->album},
                   album);
}

static void draw_progress(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    Rect bar =
        line_area(theme, layout(now_playing)->progress_top, layout(now_playing)->progress_height);
    bool volume = showing(now_playing, NOW_PLAYING_FEEDBACK_VOLUME);
    int filled = volume ? now_playing_volume_width(now_playing->status.volume, bar.width)
                        : now_playing_progress_width(shown_position(now_playing),
                                                     now_playing->status.duration_ms, bar.width);
    Color fill = now_playing->scrubbing || volume ? theme->colors.accent_alt : theme->colors.accent;
    canvas_fill_rect(canvas, bar, theme->colors.divider);
    canvas_fill_rect(canvas, (Rect){bar.x, bar.y, filled, bar.height}, fill);
    int cursor = filled > 1 ? filled - 2 : 0;
    canvas_fill_rect(canvas, (Rect){bar.x + cursor, bar.y - 1, 2, bar.height + 2},
                     theme->colors.text);
}

static void draw_right_aligned(Canvas *canvas, TextStyle style, Rect area, const char *text)
{
    int width = text_width(style.font, text);
    Rect right = {area.x + area.width - width, area.y, width, area.height};
    label_draw(canvas, style, (Label){.area = right, .text = text});
}

static uint32_t whole_seconds_left(uint32_t position_ms, uint32_t duration_ms)
{
    uint32_t position_s = position_ms / MS_PER_SECOND;
    uint32_t duration_s = duration_ms / MS_PER_SECOND;
    return duration_s > position_s ? (duration_s - position_s) * MS_PER_SECOND : 0;
}

static void draw_times(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    uint32_t position_ms = shown_position(now_playing);
    uint32_t duration_ms = now_playing->status.duration_ms;
    uint32_t remaining_ms = whole_seconds_left(position_ms, duration_ms);
    char elapsed[TIME_CAPACITY];
    char remaining[TIME_CAPACITY] = "-";
    format_duration(position_ms, elapsed, sizeof elapsed);
    format_duration(remaining_ms, remaining + 1, sizeof remaining - 1);
    const char *left = elapsed;
    if (showing(now_playing, NOW_PLAYING_FEEDBACK_VOLUME)) {
        left = i18n_text(STR_VOLUME);
        StringBuilder level = string_builder_make(remaining, sizeof remaining);
        string_builder_append_unsigned(&level, now_playing->status.volume);
    }
    Rect area =
        line_area(theme, layout(now_playing)->times_top, layout(now_playing)->small_line_height);
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    Rect icon = {area.x, area.y + (area.height - TRANSPORT_ICON) / 2, TRANSPORT_ICON,
                 TRANSPORT_ICON};
    IconId state = now_playing->status.state == PLAYER_PAUSED ? ICON_PAUSE : ICON_PLAY;
    icon_draw(canvas, state, icon, (IconInk){theme->colors.accent, theme->colors.accent});
    area.x += TRANSPORT_ICON + theme->metrics.detail_gap;
    area.width -= TRANSPORT_ICON + theme->metrics.detail_gap;
    label_draw(canvas, style, (Label){.area = area, .text = left});
    draw_right_aligned(canvas, style, area, remaining);
}

static void modes_text(const NowPlayingScreen *now_playing, char *buffer, size_t capacity)
{
    const PlayerStatus *status = &now_playing->status;
    const Settings *settings = now_playing->config.settings;
    StringBuilder builder = string_builder_make(buffer, capacity);
    if (status->repeat != REPEAT_OFF) {
        string_builder_append(
            &builder, i18n_text(status->repeat == REPEAT_ONE ? STR_REPEAT_ONE : STR_REPEAT_ALL));
    }
    if (settings != NULL && settings->playback_speed != SETTINGS_SPEED_NORMAL &&
        settings->playback_speed < SETTINGS_SPEED_COUNT) {
        if (buffer[0] != '\0') {
            string_builder_append(&builder, SEPARATOR);
        }
        string_builder_append(&builder, i18n_text(SETTINGS_SPEED_LABELS[settings->playback_speed]));
    }
}

static void draw_format(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    HalAudioFormat format = now_playing->status.format;
    const char *codec = now_playing_codec_name(format.codec);
    char text[STATUS_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append(&builder, codec);
    char stream[STATUS_CAPACITY];
    now_playing_stream_text(format, stream, sizeof stream);
    if (codec[0] != '\0' && stream[0] != '\0') {
        string_builder_append(&builder, SEPARATOR);
    }
    string_builder_append(&builder, stream);
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_tertiary};
    label_draw(canvas, style,
               (Label){column_area(now_playing, layout(now_playing)->format_top,
                                   layout(now_playing)->small_line_height),
                       text});
}

static void draw_details(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    const Theme *theme = now_playing->config.theme;
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_tertiary};
    char detail[STATUS_CAPACITY];
    now_playing_detail_text(&now_playing->status, detail, sizeof detail);
    label_draw(canvas, style,
               (Label){column_area(now_playing, layout(now_playing)->detail_top,
                                   layout(now_playing)->small_line_height),
                       detail});
    char modes[STATUS_CAPACITY];
    modes_text(now_playing, modes, sizeof modes);
    const CassetteState *tape =
        now_playing->config.player != NULL ? &now_playing->config.player->cassette : NULL;
    if (tape != NULL && tape->active) {
        StringBuilder side = string_builder_make(modes, sizeof modes);
        if (tape->flip) {
            string_builder_append(
                &side, i18n_text(tape->side == 0 ? STR_SIDE_A_FINISHED : STR_SIDE_B_FINISHED));
        } else {
            char entry[16];
            StringBuilder number = string_builder_make(entry, sizeof entry);
            uint32_t first = tape->side == 0 ? 0 : tape->split;
            string_builder_append_unsigned(&number, now_playing->status.place.number - first);
            i18n_format_text(&side, tape->side == 0 ? STR_TAPE_SIDE_A : STR_TAPE_SIDE_B, entry);
        }
    }
    if (now_playing->scrubbing) {
        StringBuilder seeking = string_builder_make(modes, sizeof modes);
        string_builder_append(&seeking, i18n_text(STR_SEEKING));
    } else if (now_playing->status.ab_repeat != 0 ||
               (now_playing->repeat_feedback_ms != 0 &&
                now_playing->now_ms - now_playing->repeat_feedback_ms < NOW_PLAYING_BADGE_MS)) {
        static const StringId states[] = {STR_AB_OFF, STR_AB_A, STR_AB_LOOP};
        StringBuilder repeat = string_builder_make(modes, sizeof modes);
        string_builder_append(&repeat, i18n_text(states[now_playing->status.ab_repeat]));
    } else if (modes[0] == 0 && now_playing->config.lyrics != NULL &&
               (now_playing->config.settings == NULL ||
                settings_button_action(now_playing->config.settings, PLAYBACK_MENU_HOLD) ==
                    BUTTON_ACTION_DEFAULT)) {
        StringBuilder hint = string_builder_make(modes, sizeof modes);
        string_builder_append(&hint, i18n_text(STR_LYRICS_HOLD_HINT));
    }
    label_draw(canvas, style,
               (Label){column_area(now_playing, layout(now_playing)->modes_top,
                                   layout(now_playing)->small_line_height),
                       modes});
}

static void draw_badge_icon(Canvas *canvas, NowPlayingFeedback feedback, Rect area, Color color)
{
    switch (feedback) {
    case NOW_PLAYING_FEEDBACK_NEXT:
        icon_draw(canvas, ICON_NEXT, area, (IconInk){color, color});
        return;
    case NOW_PLAYING_FEEDBACK_PREVIOUS:
        icon_draw(canvas, ICON_PREVIOUS, area, (IconInk){color, color});
        return;
    case NOW_PLAYING_FEEDBACK_PAUSE:
        icon_draw(canvas, ICON_PAUSE, area, (IconInk){color, color});
        return;
    case NOW_PLAYING_FEEDBACK_PLAY:
        icon_draw(canvas, ICON_PLAY, area, (IconInk){color, color});
        return;
    default:
        return;
    }
}

static void draw_badge(const NowPlayingScreen *now_playing, Canvas *canvas)
{
    NowPlayingFeedback feedback = now_playing->feedback;
    if (feedback == NOW_PLAYING_FEEDBACK_VOLUME || !showing(now_playing, feedback)) {
        return;
    }
    const Theme *theme = now_playing->config.theme;
    int center_x = cover_left(now_playing) + layout(now_playing)->cover_size / 2;
    int center_y = layout(now_playing)->cover_top + layout(now_playing)->cover_size / 2;
    Rect badge = {center_x - BADGE_SIZE / 2, center_y - BADGE_SIZE / 2, BADGE_SIZE, BADGE_SIZE};
    Rect icon = {center_x - BADGE_ICON_SIZE / 2, center_y - BADGE_ICON_SIZE / 2, BADGE_ICON_SIZE,
                 BADGE_ICON_SIZE};
    canvas_fill_rect(canvas, badge, theme->colors.background);
    draw_badge_icon(canvas, feedback, icon, theme->colors.text);
}

static void draw_nothing_playing(const Theme *theme, Canvas *canvas)
{
    message_draw(canvas, theme, theme_content_area(theme), i18n_text(STR_NOTHING_PLAYING));
}

static void draw(void *context, Canvas *canvas)
{
    const NowPlayingScreen *now_playing = context;
    const Theme *theme = now_playing->config.theme;
    Rect content = theme_content_area(theme);
    bool artwork = theme_art_draw(theme, canvas, THEME_ART_NOW_PLAYING, content);
    if (!artwork) {
        canvas_fill_rect(canvas, content, theme->colors.background);
    }
    Rect header = theme_header_area(theme);
    canvas_fill_rect(canvas, header, theme->colors.header_background);
    canvas_fill_rect(canvas, (Rect){0, header.height - 1, header.width, 1}, theme->colors.divider);
    header_draw_app_name(canvas, theme);
    TextStyle heading = {.font = theme->fonts.small, .color = theme->colors.header_text};
    Rect title = header_title_area(theme, heading.font, now_playing->screen.title);
    label_draw(canvas, heading, (Label){title, now_playing->screen.title});
    if (!now_playing->status.has_track) {
        draw_nothing_playing(theme, canvas);
        return;
    }
    bool cassette = now_playing->config.settings != NULL &&
                    settings_now_playing_style(now_playing->config.settings) != NOW_PLAYING_COVER &&
                    now_playing->config.cassette_draw != NULL;
    if (cassette) {
        now_playing->config.cassette_draw(now_playing, canvas);
    } else {
        draw_cover(now_playing, canvas, artwork);
        draw_track_text(now_playing, canvas);
    }
    draw_badge(now_playing, canvas);
    draw_progress(now_playing, canvas);
    if (!cassette || theme_is_portrait(theme)) {
        draw_format(now_playing, canvas);
        draw_details(now_playing, canvas);
    }
    draw_times(now_playing, canvas);
}

void now_playing_screen_attach(NowPlayingScreen *now_playing)
{
    now_playing->screen.enter = enter;
    now_playing->screen.handle_key = handle_key;
    now_playing->screen.update = update;
    now_playing->screen.draw = draw;
}

Screen *now_playing_screen_init(NowPlayingScreen *now_playing, NowPlayingConfig config)
{
    now_playing_screen_setup(now_playing, config);
    now_playing_screen_attach(now_playing);
    return &now_playing->screen;
}
