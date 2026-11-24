#include "now_playing_screen.h"

#include <stddef.h>

#include "app/debug_loop.h"
#include "board_layout.h"
#include "cassette_art.h"
#include "core/deadline.h"
#include "core/format.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "fonts/fonts.h"
#include "gfx/image.h"
#include "hal/power.h"
#include "home_art.h"
#include "neon_art.h"
#include "playback/spoken.h"
#include "settings/settings.h"
#include "ui/footer_hints.h"
#include "ui/header.h"
#include "ui/icons.h"
#include "ui/label.h"
#include "ui/marquee.h"
#include "ui/message_screen.h"

enum {
    MS_PER_SECOND = 1000,
    ART_SPINE = 3,
    ART_SHADOW = 5,
    LINE_GAP = 3,
    TRANSPORT_ICON = 12,
    TRANSPORT_GAP = 6,
    TRANSPORT_RULE = 8,
    TIME_CAPACITY = 16,
    BADGE_SIZE = 40,
    BADGE_ICON_SIZE = 18,
    STATUS_CAPACITY = 64,
    BITRATE_CAPACITY = 24,
    RATE_CAPACITY = 16
};

enum {
    SKIP_TOAST_CAPACITY = 32
};

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

static void join(StringBuilder *builder, const char *part)
{
    if (part[0] == '\0') {
        return;
    }
    if (builder->length > 0) {
        string_builder_append(builder, SEPARATOR);
    }
    string_builder_append(builder, part);
}

void now_playing_format_text(const PlayerStatus *status, char *buffer, size_t capacity,
                             unsigned parts)
{
    HalAudioFormat format = status->format;
    StringBuilder builder = string_builder_make(buffer, capacity);
    char part[BITRATE_CAPACITY + RATE_CAPACITY];
    if ((parts & NOW_PLAYING_FORMAT_STREAM) != 0) {
        join(&builder, now_playing_codec_name(format.codec));
        now_playing_stream_text(format, part, sizeof part);
        join(&builder, part);
    }
    part[0] = '\0';
    if ((parts & NOW_PLAYING_FORMAT_BITRATE) != 0 && format.codec != HAL_AUDIO_CODEC_UNKNOWN &&
        format.bitrate_kbps > 0) {
        format_bitrate(format.bitrate_kbps, part, sizeof part);
    }
    join(&builder, part);
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
    return !deadline_elapsed(now_ms, started_ms, duration);
}

int now_playing_volume_width(uint8_t volume, int width)
{
    uint8_t level = volume < SETTINGS_VOLUME_MAX ? volume : SETTINGS_VOLUME_MAX;
    return level * width / SETTINGS_VOLUME_MAX;
}

static uint32_t clamp_ms(uint32_t value, uint32_t high)
{
    return value > high ? high : value;
}

uint32_t now_playing_scrub_rate(uint32_t duration_ms, uint32_t held_ms)
{
    if (held_ms >= NOW_PLAYING_SCRUB_FAST_AFTER_MS) {
        return clamp_ms(duration_ms / NOW_PLAYING_SCRUB_FAST_DIVISOR,
                        NOW_PLAYING_SCRUB_FAST_MAX_MS_PER_S);
    }
    return clamp_ms(duration_ms / NOW_PLAYING_SCRUB_DIVISOR, NOW_PLAYING_SCRUB_MAX_MS_PER_S);
}

uint32_t now_playing_scrub_distance(uint32_t duration_ms, uint32_t held_ms)
{
    uint32_t slow_ms = clamp_ms(held_ms, NOW_PLAYING_SCRUB_FAST_AFTER_MS);
    uint64_t moved =
        ((uint64_t)now_playing_scrub_rate(duration_ms, 0) * slow_ms +
         (uint64_t)now_playing_scrub_rate(duration_ms, NOW_PLAYING_SCRUB_FAST_AFTER_MS) *
             (held_ms - slow_ms)) /
        MS_PER_SECOND;
    return moved < duration_ms ? (uint32_t)moved : duration_ms;
}

uint32_t now_playing_scrub_preview(const NowPlayingScrub *scrub, uint32_t now_ms)
{
    uint32_t held_ms = now_ms - scrub->started_ms;
    uint32_t distance =
        clamp_ms(now_playing_scrub_distance(scrub->duration_ms, held_ms > INT32_MAX ? 0 : held_ms),
                 INT32_MAX);
    return now_playing_scrub_target(scrub->base_ms, scrub->duration_ms,
                                    scrub->back ? -(int32_t)distance : (int32_t)distance);
}

static void scrub_begin(NowPlayingScreen *now_playing, bool back)
{
    PlayerStatus status;
    if (!now_playing_scrub_status(now_playing, &status, now_playing->now_ms)) {
        return;
    }
    uint32_t base = now_playing_scrub_target(status.position_ms, status.duration_ms, 0);
    now_playing->scrub = (NowPlayingScrub){.started_ms = now_playing->now_ms,
                                           .base_ms = base,
                                           .target_ms = base,
                                           .shown_ms = base,
                                           .duration_ms = status.duration_ms,
                                           .token = player_token(now_playing->config.player),
                                           .active = true,
                                           .back = back,
                                           .cable = hal_power_cable()};
}

static ScreenAction run_command(NowPlayingScreen *now_playing, NowPlayingCommand command)
{
    switch (command) {
    case NOW_PLAYING_SEEK_FORWARD_START:
    case NOW_PLAYING_SEEK_BACK_START:
        scrub_begin(now_playing, command == NOW_PLAYING_SEEK_BACK_START);
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
        return screen_action_pop_to_root();
    default:
        now_playing_player_command(now_playing, command, now_playing->now_ms);
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
        if (covers_tracks_separate(now_playing->album, album.track_count)) {
            now_playing->album_index = LIBRARY_NONE;
        }
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

StringId now_playing_skip_outcome(bool next, PlayerStatus before, PlayerStatus after)
{
    if (before.spoken) {
        return next ? STR_JUMPED_FORWARD : STR_JUMPED_BACK;
    }
    bool moved = after.track != before.track || after.place.number != before.place.number;
    bool restarted = !moved && after.position_ms < before.position_ms;
    if (next) {
        return moved || restarted ? STR_SKIPPED_TO_NEXT : STR_SKIPPED_NOTHING;
    }
    if (moved) {
        return STR_SKIPPED_TO_PREVIOUS;
    }
    return restarted ? STR_SKIPPED_TO_START : STRING_COUNT;
}

static void announce_skip(NowPlayingScreen *now_playing, bool next, PlayerStatus before,
                          uint32_t now_ms)
{
    StringId outcome = now_playing_skip_outcome(next, before, now_playing->status);
    if (outcome == STRING_COUNT) {
        return;
    }
    char text[SKIP_TOAST_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    uint32_t jump_ms = next ? SPOKEN_JUMP_FORWARD_MS : SPOKEN_JUMP_BACK_MS;
    i18n_format_counts(&builder, outcome, jump_ms / MS_PER_SECOND, 0);
    toast_show(now_playing->config.toast, text, now_ms);
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    NowPlayingScreen *now_playing = context;
    now_playing->now_ms = now_ms;
    NowPlayingCommand command = now_playing_command_for(event);
    PlayerStatus before = player_status(now_playing->config.player);
    ScreenAction action = run_command(now_playing, command);
    refresh(now_playing, now_ms);
    NowPlayingFeedback feedback = now_playing_feedback_for(command, now_playing->status.state);
    if (feedback != NOW_PLAYING_FEEDBACK_NONE) {
        now_playing->feedback = feedback;
        now_playing->feedback_started_ms = now_ms;
    }
    bool skip = command == NOW_PLAYING_NEXT || command == NOW_PLAYING_PREVIOUS;
    if (event.chord && skip && now_playing->config.toast != NULL) {
        announce_skip(now_playing, command == NOW_PLAYING_NEXT, before, now_ms);
    }
    return action;
}

static void enter(void *context, uint32_t now_ms)
{
    NowPlayingScreen *now_playing = context;
    now_playing->has_text = false;
    now_playing->destination = NULL;
    now_playing->feedback = NOW_PLAYING_FEEDBACK_NONE;
    refresh(now_playing, now_ms);
}

static bool showing(const NowPlayingScreen *now_playing, NowPlayingFeedback feedback)
{
    return now_playing->feedback == feedback &&
           now_playing_feedback_visible(feedback, now_playing->feedback_started_ms,
                                        now_playing->now_ms);
}

static void load_track_cover(CoverCache *cache, uint16_t track)
{
    if (cache->full_state != COVER_EMPTY && cache->full_album == LIBRARY_NONE &&
        cache->full_track == track) {
        return;
    }
    char path[PLAYER_PATH_CAPACITY];
    LibraryTrack row;
    uint32_t record;
    cache->full_album = LIBRARY_NONE;
    cache->full_track = track;
    cache->full_state = COVER_MISSING;
    if (cache->available && library_read_track(cache->library, track, &row) &&
        library_read_string(cache->library, row.path, path, sizeof path) &&
        covers_find(&cache->covers, covers_track_key(path), &record) &&
        covers_read(&cache->covers, record, COVER_FULL, cache->full)) {
        cache->full_state = COVER_LOADED;
    }
    debug_loop_event(DEBUG_EVENT_COVER,
                     DEBUG_COVER_FULL |
                         (cache->full_state == COVER_LOADED ? DEBUG_COVER_FOUND : 0u));
}

static void update(void *context, uint32_t now_ms)
{
    NowPlayingScreen *now_playing = context;
    refresh(now_playing, now_ms);
    if (now_playing->scrub.active) {
        now_playing->scrub.target_ms = now_playing_scrub_preview(&now_playing->scrub, now_ms);
    }
    if (!now_playing->config.player->cassette.flip) {
        now_playing->flip_prompted = false;
    } else if (!now_playing->flip_prompted && now_playing->config.flip != NULL) {
        now_playing->flip_prompted = true;
        now_playing->destination = now_playing->config.flip;
    }
    if (now_playing->config.covers != NULL && now_playing->status.has_track) {
        if (now_playing->album_index == LIBRARY_NONE) {
            load_track_cover(now_playing->config.covers, now_playing->text_track);
        } else {
            cover_cache_load_full(now_playing->config.covers, now_playing->album_index);
        }
    }
}

static bool current_cover(const NowPlayingScreen *now_playing, Image *image)
{
    const CoverCache *covers = now_playing->config.covers;
    return covers != NULL && now_playing->has_text &&
           (now_playing->album_index != LIBRARY_NONE ||
            covers->full_track == now_playing->text_track) &&
           cover_cache_full(covers, now_playing->album_index, image) == COVER_LOADED;
}

static NowPlayingStyle style_of(const NowPlayingScreen *now_playing)
{
    return now_playing->config.settings != NULL
               ? settings_now_playing_style(now_playing->config.settings)
               : NOW_PLAYING_COVER;
}

static bool styled(const NowPlayingScreen *now_playing)
{
    return style_of(now_playing) != NOW_PLAYING_COVER && now_playing->config.cassette_draw != NULL;
}

static bool staged(const NowPlayingScreen *now_playing)
{
    return styled(now_playing) && style_of(now_playing) >= NOW_PLAYING_VU;
}

int now_playing_title_lift(const Theme *theme)
{
    const char *title = i18n_text(STR_NOW_PLAYING);
    const Font *font = theme->fonts.header;
    int width = theme->metrics.screen_width - 2 * theme->metrics.padding;
    if (!theme_is_portrait(theme) || text_width(font, title) <= header_title_bounds(theme).width) {
        return 0;
    }
    return label_wrap_count(font, title, width) * font_line_height(font);
}

NowPlayingFrame now_playing_frame(const Theme *theme, bool stage)
{
    return board_now_playing_frame(theme, stage, now_playing_title_lift(theme));
}

static bool previewing(const NowPlayingScreen *now_playing)
{
    const NowPlayingScrub *scrub = &now_playing->scrub;
    return scrub->active && now_playing->config.player != NULL &&
           scrub->token == player_token(now_playing->config.player);
}

static void watch_position(const NowPlayingScreen *now_playing, MarqueeBand band, Rect area)
{
    marquee_watch_area(previewing(now_playing) ? MARQUEE_BAND_TEXT : band, area);
}

static uint32_t shown_position(const NowPlayingScreen *now_playing)
{
    const PlayerStatus *status = &now_playing->status;
    if (previewing(now_playing)) {
        return now_playing_scrub_preview(&now_playing->scrub, now_playing->now_ms);
    }
    return status->state == PLAYER_PLAYING && status->position_ms < status->duration_ms
               ? status->position_ms / MS_PER_SECOND * MS_PER_SECOND
               : status->position_ms;
}

static Rect stage_of(const Theme *theme, Rect art)
{
    int width = theme_is_portrait(theme) ? theme->metrics.screen_width
                                         : art.x + art.width + theme->metrics.padding - ART_SPINE;
    return (Rect){0, art.y - theme->metrics.padding, width,
                  art.height + theme->metrics.padding + ART_SHADOW + 2};
}

static bool touches(const Canvas *canvas, Rect area)
{
    return !rect_is_empty(rect_intersection(canvas->clip, area));
}

static void draw_stage(const NowPlayingScreen *now_playing, Canvas *canvas, bool artwork, Rect art)
{
    const Theme *theme = now_playing->config.theme;
    if (!artwork) {
        home_art_draw_backdrop(canvas, stage_of(theme, art), theme);
    }
    neon_art_frame(canvas, (Rect){art.x - 4, art.y - 4, art.width + 8, art.height + 8},
                   neon_art_ink(theme, theme->colors.accent));
}

static void draw_cover(const NowPlayingScreen *now_playing, Canvas *canvas, bool artwork, Rect art)
{
    Image cover;
    const Theme *theme = now_playing->config.theme;
    if (theme->horizon) {
        draw_stage(now_playing, canvas, artwork, art);
    }
    canvas_fill_rect(canvas, (Rect){art.x + ART_SHADOW, art.y + ART_SHADOW, art.width, art.height},
                     theme->colors.backdrop_bottom);
    canvas_fill_rect(canvas, (Rect){art.x - ART_SPINE, art.y, ART_SPINE, art.height},
                     theme->colors.accent_alt);
    if (current_cover(now_playing, &cover)) {
        image_draw_quad(
            canvas, cover,
            (Quad){art.x, art.width, art.y, art.height, art.y + ART_SPINE, art.height - ART_SPINE});
        return;
    }
    canvas_fill_rect(canvas, art, theme->colors.backdrop_top);
    Rect tape = {art.x + (art.width - CASSETTE_WIDTH) / 2,
                 art.y + (art.height - CASSETTE_HEIGHT) / 2, CASSETTE_WIDTH, CASSETTE_HEIGHT};
    watch_position(now_playing, MARQUEE_BAND_TAPE, tape);
    cassette_art_draw(canvas, tape, theme,
                      (CassetteTape){shown_position(now_playing), now_playing->status.duration_ms,
                                     FIRMWARE_SHORT_NAME});
}

typedef struct ColumnFlow {
    const NowPlayingScreen *screen;
    Canvas *canvas;
    Rect area;
} ColumnFlow;

static void draw_scrolling(const NowPlayingScreen *now_playing, Canvas *canvas, Label label,
                           TextStyle style)
{
    int width = text_width(style.font, label.text);
    if (width <= label.area.width) {
        label_draw(canvas, style, label);
        return;
    }
    MarqueeTiming timing = now_playing->config.theme->marquee;
    uint32_t elapsed = now_playing->now_ms - now_playing->text_changed_ms;
    int offset = marquee_offset_in(timing, width, label.area, elapsed);
    label_draw_scrolling(canvas, style, label, (Marquee){.offset = offset, .gap = timing.gap_px});
}

static void flow_skip(ColumnFlow *flow, int height)
{
    flow->area.y += height + LINE_GAP;
    flow->area.height -= height + LINE_GAP;
}

static void flow_text(ColumnFlow *flow, TextStyle style, const char *text, int lines)
{
    int line = font_line_height(style.font);
    if (text[0] == '\0') {
        return;
    }
    bool still = staged(flow->screen);
    int count = lines > 1 ? label_wrap_count(style.font, text, flow->area.width) : 1;
    int shown = count < lines ? count : lines;
    bool wrap = count > 1 && (count <= lines || still);
    Label label = {{flow->area.x, flow->area.y, flow->area.width, wrap ? line * shown : line},
                   text};
    if (label.area.height > flow->area.height) {
        flow->area.height = -1;
        return;
    }
    if (wrap) {
        label_draw_paragraph(flow->canvas, style, label);
    } else if (still) {
        label_draw(flow->canvas, style, label);
    } else {
        draw_scrolling(flow->screen, flow->canvas, label, style);
    }
    flow_skip(flow, label.area.height);
}

static void draw_names(ColumnFlow *flow)
{
    const NowPlayingScreen *now_playing = flow->screen;
    const Theme *theme = now_playing->config.theme;
    bool portrait = theme_is_portrait(theme);
    bool large = board_text_size(theme) == TEXT_SIZE_LARGE && !portrait;
    const Font *album = large ? &FONT_INTER_REGULAR_22 : theme->fonts.small;
    flow_text(flow, (TextStyle){theme->fonts.title, theme->colors.text, false}, now_playing->title,
              portrait ? 2 : 1);
    int artist = font_line_height(theme->fonts.body);
    int reserve = flow->area.y + 2 * (artist + LINE_GAP);
    flow_text(flow, (TextStyle){theme->fonts.body, theme->colors.accent, false},
              now_playing->artist, 2);
    int spare = reserve - flow->area.y;
    if (staged(now_playing) && !portrait && spare > 0 &&
        flow->area.height - spare >= font_line_height(album)) {
        flow_skip(flow, spare - LINE_GAP);
    }
    flow_text(flow, (TextStyle){album, theme->colors.text_secondary, false}, now_playing->album, 1);
}

static void draw_format(ColumnFlow *flow)
{
    const NowPlayingScreen *now_playing = flow->screen;
    const Theme *theme = now_playing->config.theme;
    TextStyle style = {theme->fonts.small, theme->colors.text_secondary, false};
    char text[STATUS_CAPACITY];
    now_playing_format_text(&now_playing->status, text, sizeof text, NOW_PLAYING_FORMAT_ALL);
    if (text_width(style.font, text) <= flow->area.width) {
        flow_text(flow, style, text, 1);
        return;
    }
    now_playing_format_text(&now_playing->status, text, sizeof text, NOW_PLAYING_FORMAT_STREAM);
    flow_text(flow, style, text, 1);
    now_playing_format_text(&now_playing->status, text, sizeof text, NOW_PLAYING_FORMAT_BITRATE);
    flow_text(flow, style, text, 1);
}

static void draw_column(const NowPlayingScreen *now_playing, Canvas *canvas, Rect column)
{
    bool cassette = styled(now_playing) && !staged(now_playing);
    if (cassette && !theme_is_portrait(now_playing->config.theme)) {
        return;
    }
    ColumnFlow flow = {now_playing, canvas, column};
    if (!cassette) {
        draw_names(&flow);
    }
    draw_format(&flow);
}

static void draw_progress(const NowPlayingScreen *now_playing, Canvas *canvas, Rect bar)
{
    const Theme *theme = now_playing->config.theme;
    bool preview = previewing(now_playing);
    bool volume = !preview && showing(now_playing, NOW_PLAYING_FEEDBACK_VOLUME);
    int filled = volume ? now_playing_volume_width(now_playing->status.volume, bar.width)
                        : now_playing_progress_width(shown_position(now_playing),
                                                     now_playing->status.duration_ms, bar.width);
    Color fill = volume ? theme->colors.accent_alt : theme->colors.accent;
    watch_position(now_playing, MARQUEE_BAND_TIME,
                   (Rect){bar.x, bar.y - 3, bar.width, bar.height + 6});
    canvas_fill_rect(canvas, bar, theme->colors.divider);
    canvas_fill_rect(canvas, (Rect){bar.x, bar.y, filled, bar.height}, fill);
    int reach = preview ? 3 : 2;
    int cursor = filled > 1 ? filled - 2 : 0;
    canvas_fill_rect(canvas, (Rect){bar.x + cursor, bar.y - reach, 2, bar.height + 2 * reach},
                     theme->colors.text);
}

static void draw_aligned(Canvas *canvas, TextStyle style, Rect area, const char *text, int share)
{
    int width = text_width(style.font, text);
    Rect place = {area.x + (area.width - width) * share / 2, area.y, width, area.height};
    label_draw(canvas, style, (Label){.area = place, .text = text});
}

static uint32_t whole_seconds_left(uint32_t position_ms, uint32_t duration_ms)
{
    uint32_t position_s = position_ms / MS_PER_SECOND;
    uint32_t duration_s = duration_ms / MS_PER_SECOND;
    return duration_s > position_s ? (duration_s - position_s) * MS_PER_SECOND : 0;
}

static void draw_place(const NowPlayingScreen *now_playing, Canvas *canvas, TextStyle style,
                       Rect area)
{
    char place[TIME_CAPACITY * 2];
    StringBuilder builder = string_builder_make(place, sizeof place);
    if (now_playing->status.place.total > 0) {
        i18n_format_counts(&builder, STR_PLACE, now_playing->status.place.number,
                           now_playing->status.place.total);
    }
    draw_aligned(canvas, style, area, place, 1);
}

static void draw_times(const NowPlayingScreen *now_playing, Canvas *canvas, Rect area)
{
    const Theme *theme = now_playing->config.theme;
    uint32_t position_ms = shown_position(now_playing);
    uint32_t remaining_ms = whole_seconds_left(position_ms, now_playing->status.duration_ms);
    char elapsed[TIME_CAPACITY];
    char remaining[TIME_CAPACITY] = "-";
    format_duration(position_ms, elapsed, sizeof elapsed);
    format_duration(remaining_ms, remaining + 1, sizeof remaining - 1);
    const char *left = elapsed;
    const char *right = remaining;
    bool preview = previewing(now_playing);
    if (preview) {
        right = i18n_text(STR_SCRUB_PREVIEW);
        marquee_watch(MARQUEE_STEP_MS);
    } else if (showing(now_playing, NOW_PLAYING_FEEDBACK_VOLUME)) {
        left = i18n_text(STR_VOLUME);
        StringBuilder level = string_builder_make(remaining, sizeof remaining);
        string_builder_append_unsigned(&level, now_playing->status.volume);
    }
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    watch_position(now_playing, MARQUEE_BAND_TIME, area);
    draw_place(now_playing, canvas, style, area);
    Rect icon = {area.x, area.y + (area.height - TRANSPORT_ICON) / 2, TRANSPORT_ICON,
                 TRANSPORT_ICON};
    IconId state = now_playing->status.state == PLAYER_PAUSED ? ICON_PAUSE : ICON_PLAY;
    icon_draw(canvas, state, icon, (IconInk){theme->colors.accent, theme->colors.accent});
    Rect text = {area.x + TRANSPORT_ICON + TRANSPORT_GAP, area.y,
                 area.width - TRANSPORT_ICON - TRANSPORT_GAP, area.height};
    label_draw(canvas, style, (Label){.area = text, .text = left});
    style.color = preview ? theme->colors.accent : style.color;
    draw_aligned(canvas, style, area, right, 2);
}

static void modes_text(const NowPlayingScreen *now_playing, char *buffer, size_t capacity)
{
    const PlayerStatus *status = &now_playing->status;
    const Settings *settings = now_playing->config.settings;
    StringBuilder builder = string_builder_make(buffer, capacity);
    if (status->repeat != REPEAT_OFF) {
        join(&builder, i18n_text(status->repeat == REPEAT_ONE ? STR_REPEAT_ONE : STR_REPEAT_ALL));
    }
    if (settings != NULL && settings->playback_speed != SETTINGS_SPEED_NORMAL &&
        settings->playback_speed < SETTINGS_SPEED_COUNT) {
        join(&builder, i18n_text(SETTINGS_SPEED_LABELS[settings->playback_speed]));
    }
}

static void tape_text(const NowPlayingScreen *now_playing, char *buffer, size_t capacity)
{
    const CassetteState *tape = &now_playing->config.player->cassette;
    StringBuilder side = string_builder_make(buffer, capacity);
    if (tape->flip) {
        string_builder_append(
            &side, i18n_text(tape->side == 0 ? STR_SIDE_A_FINISHED : STR_SIDE_B_FINISHED));
        return;
    }
    char entry[16];
    StringBuilder number = string_builder_make(entry, sizeof entry);
    uint32_t first = tape->side == 0 ? 0 : tape->split;
    string_builder_append_unsigned(&number, now_playing->status.place.number - first);
    i18n_format_text(&side, tape->side == 0 ? STR_TAPE_SIDE_A : STR_TAPE_SIDE_B, entry);
}

static void status_text(const NowPlayingScreen *now_playing, char *buffer, size_t capacity)
{
    modes_text(now_playing, buffer, capacity);
    if (now_playing->config.player != NULL && now_playing->config.player->cassette.active) {
        tape_text(now_playing, buffer, capacity);
    }
    if (now_playing->status.ab_repeat != 0 ||
        (now_playing->repeat_feedback_ms != 0 &&
         !deadline_elapsed(now_playing->now_ms, now_playing->repeat_feedback_ms,
                           NOW_PLAYING_BADGE_MS))) {
        static const StringId states[] = {STR_AB_OFF, STR_AB_A, STR_AB_LOOP};
        StringBuilder repeat = string_builder_make(buffer, capacity);
        string_builder_append(&repeat, i18n_text(states[now_playing->status.ab_repeat]));
    }
}

static const FooterHint LYRICS_HINT[] = {{FOOTER_KEY_HOLD_MENU, STR_HINT_SHOW_LYRICS}};
static const FooterHints LYRICS_CUE = {
    .items = LYRICS_HINT, .count = 1, .align = FOOTER_ALIGN_START};

static bool lyrics_cued(const NowPlayingScreen *now_playing)
{
    return now_playing->config.lyrics != NULL &&
           (now_playing->config.settings == NULL ||
            settings_button_action(now_playing->config.settings, PLAYBACK_MENU_HOLD) ==
                BUTTON_ACTION_DEFAULT);
}

static void draw_cue(const NowPlayingScreen *now_playing, Canvas *canvas, Rect area)
{
    const Theme *theme = now_playing->config.theme;
    char modes[STATUS_CAPACITY];
    status_text(now_playing, modes, sizeof modes);
    if (modes[0] == '\0' && lyrics_cued(now_playing)) {
        footer_hints_draw_inline(canvas, theme, area, LYRICS_CUE);
        return;
    }
    TextStyle style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    draw_aligned(canvas, style, area, modes, theme_is_portrait(theme) ? 1 : 0);
}

static const uint8_t BADGE_ICONS[] = {
    [NOW_PLAYING_FEEDBACK_NEXT] = ICON_NEXT,
    [NOW_PLAYING_FEEDBACK_PREVIOUS] = ICON_PREVIOUS,
    [NOW_PLAYING_FEEDBACK_PAUSE] = ICON_PAUSE,
    [NOW_PLAYING_FEEDBACK_PLAY] = ICON_PLAY,
};

static Rect centred(Rect art, int size)
{
    return (Rect){art.x + (art.width - size) / 2, art.y + (art.height - size) / 2, size, size};
}

static void draw_badge(const NowPlayingScreen *now_playing, Canvas *canvas, Rect art)
{
    NowPlayingFeedback feedback = now_playing->feedback;
    if (feedback == NOW_PLAYING_FEEDBACK_VOLUME || !showing(now_playing, feedback)) {
        return;
    }
    const Theme *theme = now_playing->config.theme;
    Color ink = theme->colors.text;
    canvas_fill_rect(canvas, centred(art, BADGE_SIZE), theme->colors.background);
    icon_draw(canvas, (IconId)BADGE_ICONS[feedback], centred(art, BADGE_ICON_SIZE),
              (IconInk){ink, ink});
}

static void draw_title(const Theme *theme, Canvas *canvas, int lift)
{
    int padding = theme->metrics.padding;
    Rect area = {padding, theme->metrics.header_height + padding / 2,
                 theme->metrics.screen_width - 2 * padding, lift};
    label_draw_paragraph(canvas, (TextStyle){theme->fonts.header, theme->colors.text, false},
                         (Label){area, i18n_text(STR_NOW_PLAYING)});
}

static void draw_transport(const NowPlayingScreen *now_playing, Canvas *canvas,
                           NowPlayingFrame frame)
{
    const Theme *theme = now_playing->config.theme;
    if (theme_is_portrait(theme)) {
        canvas_fill_rect(
            canvas,
            (Rect){frame.progress.x, frame.progress.y - TRANSPORT_RULE, frame.progress.width, 1},
            theme->colors.divider);
    }
    draw_progress(now_playing, canvas, frame.progress);
    draw_times(now_playing, canvas, frame.times);
    draw_cue(now_playing, canvas, frame.cue);
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
    int lift = now_playing_title_lift(theme);
    header_draw(canvas, theme, lift > 0 ? NULL : i18n_text(STR_NOW_PLAYING));
    if (!now_playing->status.has_track) {
        message_draw(canvas, theme, content, i18n_text(STR_NOTHING_PLAYING));
        return;
    }
    NowPlayingFrame frame = now_playing_frame(theme, styled(now_playing));
    if (styled(now_playing)) {
        if (!staged(now_playing)) {
            watch_position(now_playing, MARQUEE_BAND_TIME, content);
        }
        now_playing->config.cassette_draw(now_playing, canvas);
    } else if (touches(canvas, stage_of(theme, frame.art))) {
        draw_cover(now_playing, canvas, artwork, frame.art);
    }
    if (lift > 0) {
        draw_title(theme, canvas, lift);
    }
    if (touches(canvas, frame.column)) {
        draw_column(now_playing, canvas, frame.column);
    }
    draw_badge(now_playing, canvas, frame.art);
    draw_transport(now_playing, canvas, frame);
}

void now_playing_screen_attach(NowPlayingScreen *now_playing)
{
    now_playing->screen.enter = enter;
    now_playing->screen.handle_key = handle_key;
    now_playing->screen.update = update;
    now_playing->screen.draw = draw;
    now_playing->screen.untitled_header = false;
}

Screen *now_playing_screen_init(NowPlayingScreen *now_playing, NowPlayingConfig config)
{
    now_playing_screen_setup(now_playing, config);
    now_playing_screen_attach(now_playing);
    return &now_playing->screen;
}
