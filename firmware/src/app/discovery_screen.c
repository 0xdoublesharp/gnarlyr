#include "discovery_screen.h"

#include <stddef.h>

#include "discovery_preferences.h"

#include "core/format.h"
#include "core/i18n.h"
#include "core/string_builder.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/message_screen.h"
#include "ui/navigation.h"

enum {
    ROW_BYTES = 192,
    DETAIL_BYTES = 96,
    EDIT_QUERY = 0,
    EDIT_MIN_YEAR,
    EDIT_MAX_YEAR,
    EDIT_MIN_DURATION,
    EDIT_MAX_DURATION,
    EDIT_TAG_CREATE,
    EDIT_TAG_RENAME,
    PICK_ARTIST = 0,
    PICK_ALBUM,
    PICK_GENRE,
    INFO_ROWS = 11,
    FOOTER_HEIGHT = 20,
    SUMMARY_HEIGHT = 20,
    WORK_STEPS = 64,
    WORK_BUDGET_US = 2000
};

typedef struct DiscoveryWork {
    DiscoveryScreen *owner;
    LibraryView view;
    PersonalTags tags;
    PersonalTags *tag_store;
    uint32_t track_mask;
    uint32_t restore_first;
    char labels[THEME_MAX_ROWS][ROW_BYTES];
    char details[THEME_MAX_ROWS][DETAIL_BYTES];
    char path[PLAYER_PATH_CAPACITY + 16];
    int first;
    int cached_mode;
    StringId notice;
    bool restoring;
    bool pending_search;
    bool tag_change;
    bool remove_tag;
    bool mask_known;
    bool pref_failed;
} DiscoveryWork;

static DiscoveryWork work;
static const char WORK_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/view.tmp";
static const char CHECKED[] = "\xE2\x9C\x93";
static const StringId OPTIONS[] = {STR_SEARCH_THIS_LIST, STR_SORT_BY, STR_FILTERS, STR_RESET_VIEW,
                                   STR_CLEAR_SEARCH};
static const StringId SORTS[] = {STR_ORIGINAL_ORDER, STR_TITLE,    STR_ARTIST,        STR_ALBUM,
                                 STR_YEAR,           STR_DURATION, STR_SORT_DIRECTION};
static const StringId FILTERS[] = {STR_ARTIST,       STR_ALBUM,        STR_GENRE,
                                   STR_MIN_YEAR,     STR_MAX_YEAR,     STR_MIN_DURATION,
                                   STR_MAX_DURATION, STR_PERSONAL_TAGS};
static const StringId INFO[] = {STR_TITLE,     STR_ARTIST,        STR_ALBUM,      STR_ALBUM_ARTIST,
                                STR_GENRE,     STR_YEAR,          STR_DISC_TRACK, STR_DURATION,
                                STR_FILE_PATH, STR_PERSONAL_TAGS, STR_PLAY_NEXT};
static const StringId ACTIONS[] = {STR_TRACK_INFO, STR_PERSONAL_TAGS, STR_PLAY_NEXT,
                                   STR_ADD_TO_UP_NEXT};

static void copy_text(char *to, size_t capacity, const char *text)
{
    StringBuilder builder = string_builder_make(to, capacity);
    string_builder_append(&builder, text);
}

static bool file_read(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    HalFile file;
    if (!hal_file_open(&file, WORK_PATH)) {
        return false;
    }
    bool ok = hal_file_read(file, offset, bytes, length);
    hal_file_close(&file);
    return ok;
}

static bool file_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    return hal_file_write_at(WORK_PATH, offset, bytes, length);
}

static BuilderScratch scratch_for(DiscoveryScreen *screen)
{
    if (screen->config.scratch.read != NULL) {
        return screen->config.scratch;
    }
    HalReserveResult result = hal_file_reserve(WORK_PATH, LIBRARY_VIEW_WORK_BYTES);
    if (result != HAL_RESERVE_EXISTS && result != HAL_RESERVE_CREATED) {
        return (BuilderScratch){0};
    }
    HalFile file;
    if (!hal_file_open(&file, WORK_PATH)) {
        return (BuilderScratch){0};
    }
    bool sized = file.size >= LIBRARY_VIEW_WORK_BYTES;
    hal_file_close(&file);
    return sized ? (BuilderScratch){file_read, file_write, NULL, LIBRARY_VIEW_WORK_BYTES}
                 : (BuilderScratch){0};
}

static void ensure_work(DiscoveryScreen *screen)
{
    if (work.owner == screen) {
        return;
    }
    work = (DiscoveryWork){.owner = screen, .first = -1, .cached_mode = -1};
    work.tag_store = screen->config.tags;
    if (work.tag_store == NULL) {
        work.tag_store = &work.tags;
        personal_tags_init(work.tag_store, screen->config.library);
    }
    if (!screen->initialized) {
        discovery_preferences_load(screen, work.tag_store);
    }
}

static bool busy_view(void)
{
    return work.pending_search || work.restoring || work.view.state == LIBRARY_VIEW_SCANNING ||
           work.view.state == LIBRARY_VIEW_SORTING;
}

static bool tags_busy(void)
{
    return work.tag_store != NULL && work.tag_store->result == STORE_WORKING;
}

static LibraryViewAcceptance accept_filters(void *context, uint16_t track,
                                            const LibraryTrack *record)
{
    const DiscoveryScreen *screen = context;
    if (screen->config.scope_accept != NULL) {
        LibraryViewAcceptance accepted =
            screen->config.scope_accept(screen->config.scope_context, track, record);
        if (accepted != LIBRARY_VIEW_ACCEPT) {
            return accepted;
        }
    }
    if (screen->required_tags == 0) {
        return LIBRARY_VIEW_ACCEPT;
    }
    bool accepted = false;
    if (!personal_tags_matches(work.tag_store, track, screen->required_tags, &accepted)) {
        return LIBRARY_VIEW_ACCEPT_FAILED;
    }
    return accepted ? LIBRARY_VIEW_ACCEPT : LIBRARY_VIEW_REJECT;
}

static void begin_view(DiscoveryScreen *screen)
{
    ensure_work(screen);
    work.first = -1;
    work.restoring = false;
    work.pending_search = tags_busy();
    if (!work.pending_search) {
        discovery_preferences_validate_tags(screen, work.tag_store);
        screen->query.accept = screen->required_tags != 0 || screen->config.scope_accept != NULL
                                   ? accept_filters
                                   : NULL;
        screen->query.context = screen;
        library_view_begin(&work.view, screen->config.library, &screen->query, scratch_for(screen));
        work.pref_failed = !discovery_preferences_save(screen, work.tag_store);
    }
}

static StringId mode_title(const DiscoveryScreen *screen)
{
    switch (screen->mode) {
    case DISCOVERY_OPTIONS:
        return STR_VIEW_OPTIONS;
    case DISCOVERY_SORT:
        return STR_SORT_BY;
    case DISCOVERY_FILTERS:
        return STR_FILTERS;
    case DISCOVERY_INFO:
        return STR_TRACK_INFO;
    case DISCOVERY_TAGS:
    case DISCOVERY_TAG_ACTIONS:
    case DISCOVERY_TAG_DELETE:
    case DISCOVERY_TAG_FILTER:
        return STR_PERSONAL_TAGS;
    case DISCOVERY_ACTIONS:
        return STR_TRACK_INFO;
    case DISCOVERY_EDITOR:
        if (screen->edit_kind == EDIT_QUERY) {
            return STR_SEARCH;
        }
        if (screen->edit_kind >= EDIT_TAG_CREATE) {
            return STR_TAG_NAME;
        }
        return FILTERS[screen->edit_kind + 2];
    case DISCOVERY_PICKER:
        return FILTERS[screen->picker_kind];
    default:
        return STR_SEARCH_RESULTS;
    }
}

static Rect list_area(const DiscoveryScreen *screen)
{
    Rect area = theme_content_area(screen->config.theme);
    if (screen->mode == DISCOVERY_RESULTS) {
        area.y += SUMMARY_HEIGHT;
        area.height -= SUMMARY_HEIGHT + FOOTER_HEIGHT;
    }
    return area;
}

static void fit(DiscoveryScreen *screen)
{
    list_view_fit_rows(&screen->list, list_view_rows_in(&screen->list, list_area(screen).height));
}

static void show_mode(DiscoveryScreen *screen, DiscoveryMode mode)
{
    screen->mode = (uint8_t)mode;
    screen->model.detail_below =
        mode == DISCOVERY_RESULTS || mode == DISCOVERY_FILTERS || mode == DISCOVERY_SORT;
    screen->screen.title = mode == DISCOVERY_RESULTS && screen->query.text[0] == 0 &&
                                   screen->config.scope_title != NULL
                               ? screen->config.scope_title
                               : i18n_text(mode_title(screen));
    work.first = -1;
    work.cached_mode = -1;
    list_view_init(&screen->list, &screen->model, screen->config.theme, screen->now_ms);
    fit(screen);
}

static void notice(DiscoveryScreen *screen, StringId text, DiscoveryMode back)
{
    work.notice = text;
    screen->previous_mode = (uint8_t)back;
    show_mode(screen, DISCOVERY_NOTICE);
}

static int active_tags(void)
{
    int count = 0;
    for (uint8_t tag = 0; tag < PERSONAL_TAG_LIMIT; tag++) {
        count += personal_tags_name(work.tag_store, tag) != NULL;
    }
    return count;
}

static uint8_t tag_at(int row)
{
    for (uint8_t tag = 0; tag < PERSONAL_TAG_LIMIT; tag++) {
        if (personal_tags_name(work.tag_store, tag) != NULL && row-- == 0) {
            return tag;
        }
    }
    return PERSONAL_TAG_LIMIT;
}

static LibrarySection picker_section(const DiscoveryScreen *screen)
{
    return screen->picker_kind == PICK_ARTIST  ? LIBRARY_ARTISTS
           : screen->picker_kind == PICK_ALBUM ? LIBRARY_ALBUMS
                                               : LIBRARY_GENRES;
}

static int row_count(const void *context)
{
    const DiscoveryScreen *screen = context;
    switch (screen->mode) {
    case DISCOVERY_RESULTS:
        return work.view.state == LIBRARY_VIEW_READY ? work.view.count : 0;
    case DISCOVERY_OPTIONS:
        return sizeof OPTIONS / sizeof *OPTIONS;
    case DISCOVERY_SORT:
        return sizeof SORTS / sizeof *SORTS;
    case DISCOVERY_FILTERS:
        return sizeof FILTERS / sizeof *FILTERS;
    case DISCOVERY_PICKER:
        return (int)library_count(screen->config.library, picker_section(screen)) + 1;
    case DISCOVERY_INFO:
        return INFO_ROWS;
    case DISCOVERY_TAGS:
    case DISCOVERY_TAG_FILTER:
        return active_tags() + 1;
    case DISCOVERY_TAG_ACTIONS:
    case DISCOVERY_TAG_DELETE:
        return 2;
    case DISCOVERY_ACTIONS:
        return sizeof ACTIONS / sizeof *ACTIONS;
    default:
        return 0;
    }
}

static void group_name(const Library *library, LibrarySection section, uint16_t id, char *text,
                       size_t capacity)
{
    LibraryGroup group;
    text[0] = 0;
    if (library_read_group(library, section, id, &group)) {
        library_read_string(library, group.name, text, capacity);
    }
}

static void album_name(const Library *library, uint16_t id, char *text, size_t capacity)
{
    LibraryAlbum album;
    text[0] = 0;
    if (library_read_album(library, id, &album)) {
        library_read_string(library, album.title, text, capacity);
    }
}

static uint16_t selected_track(const DiscoveryScreen *screen)
{
    uint16_t track = LIBRARY_NONE;
    if (work.view.state == LIBRARY_VIEW_READY && screen->list.selected < work.view.count) {
        library_view_read(&work.view, (uint16_t)screen->list.selected, 1, &track);
    }
    return track;
}

static void describe_result(const DiscoveryScreen *screen, int index, char *label, char *detail)
{
    uint16_t id;
    LibraryTrack track;
    const Library *library = screen->config.library;
    if (library_view_read(&work.view, (uint16_t)index, 1, &id) &&
        library_read_track(library, id, &track)) {
        library_read_string(library, track.title, label, ROW_BYTES);
        group_name(library, LIBRARY_ARTISTS, track.artist, detail, DETAIL_BYTES);
    }
}

static void describe_picker(const DiscoveryScreen *screen, int index, char *label)
{
    if (index == 0) {
        copy_text(label, ROW_BYTES, i18n_text(STR_ANY));
    } else if (screen->picker_kind == PICK_ALBUM) {
        album_name(screen->config.library, (uint16_t)(index - 1), label, ROW_BYTES);
    } else {
        group_name(screen->config.library, picker_section(screen), (uint16_t)(index - 1), label,
                   ROW_BYTES);
    }
}

static void append_info(DiscoveryScreen *screen, int index, StringBuilder *builder)
{
    const Library *library = screen->config.library;
    LibraryTrack track;
    LibraryAlbum album;
    char text[PLAYER_PATH_CAPACITY];
    if (!library_read_track(library, screen->info_track, &track)) {
        return;
    }
    bool has_album = library_read_album(library, track.album, &album);
    text[0] = 0;
    if (index == 0) {
        library_read_string(library, track.title, text, sizeof text);
    } else if (index == 1) {
        group_name(library, LIBRARY_ARTISTS, track.artist, text, sizeof text);
    } else if (index == 2) {
        album_name(library, track.album, text, sizeof text);
    } else if (index == 3 && has_album) {
        group_name(library, LIBRARY_ALBUM_ARTISTS, album.album_artist, text, sizeof text);
    } else if (index == 4) {
        group_name(library, LIBRARY_GENRES, track.genre, text, sizeof text);
    } else if (index == 5 && has_album) {
        if (album.year != 0) {
            string_builder_append_unsigned(builder, album.year);
        }
        return;
    } else if (index == 6) {
        string_builder_append_unsigned(builder, track.disc);
        string_builder_append_char(builder, '/');
        string_builder_append_unsigned(builder, track.number);
        return;
    } else if (index == 7) {
        format_duration(track.duration_ms, text, sizeof text);
    } else if (index == 8) {
        library_read_string(library, track.path, text, sizeof text);
    }
    string_builder_append(builder, text);
}

static void describe_info(DiscoveryScreen *screen, int index, char *label)
{
    StringBuilder builder = string_builder_make(label, ROW_BYTES);
    string_builder_append(&builder, i18n_text(INFO[index]));
    if (index < 9) {
        string_builder_append(&builder, ": ");
        append_info(screen, index, &builder);
    }
}

static uint32_t numeric_filter(const DiscoveryScreen *screen, int index)
{
    if (index == 3) {
        return screen->query.year_min;
    }
    if (index == 4) {
        return screen->query.year_max;
    }
    return (index == 5 ? screen->query.duration_min_ms : screen->query.duration_max_ms) / 60000;
}

static void describe_filter(const DiscoveryScreen *screen, int index, char *detail)
{
    const Library *library = screen->config.library;
    if (index == 0 && screen->query.artist != LIBRARY_NONE) {
        group_name(library, LIBRARY_ARTISTS, screen->query.artist, detail, DETAIL_BYTES);
    } else if (index == 1 && screen->query.album != LIBRARY_NONE) {
        album_name(library, screen->query.album, detail, DETAIL_BYTES);
    } else if (index == 2 && screen->query.genre != LIBRARY_NONE) {
        group_name(library, LIBRARY_GENRES, screen->query.genre, detail, DETAIL_BYTES);
    } else if (index >= 3 && index <= 6 && numeric_filter(screen, index) != 0) {
        StringBuilder builder = string_builder_make(detail, DETAIL_BYTES);
        string_builder_append_unsigned(&builder, numeric_filter(screen, index));
    } else if (index == 7 && screen->required_tags != 0) {
        copy_text(detail, DETAIL_BYTES, CHECKED);
    } else {
        copy_text(detail, DETAIL_BYTES, i18n_text(STR_ANY));
    }
}

static void describe_tag(const DiscoveryScreen *screen, int index, char *label, char *detail)
{
    bool filter = screen->mode == DISCOVERY_TAG_FILTER;
    if (index == 0) {
        copy_text(label, ROW_BYTES, i18n_text(filter ? STR_DONE : STR_NEW_TAG));
        return;
    }
    uint8_t tag = tag_at(index - 1);
    const char *name = personal_tags_name(work.tag_store, tag);
    if (name != NULL) {
        copy_text(label, ROW_BYTES, name);
        uint32_t mask = filter ? screen->required_tags : work.track_mask;
        if ((mask & (1u << tag)) != 0) {
            copy_text(detail, DETAIL_BYTES, CHECKED);
        }
    }
}

static void describe_row(DiscoveryScreen *screen, int index, char *label, char *detail)
{
    label[0] = 0;
    detail[0] = 0;
    switch (screen->mode) {
    case DISCOVERY_RESULTS:
        describe_result(screen, index, label, detail);
        break;
    case DISCOVERY_PICKER:
        describe_picker(screen, index, label);
        break;
    case DISCOVERY_INFO:
        describe_info(screen, index, label);
        break;
    case DISCOVERY_FILTERS:
        copy_text(label, ROW_BYTES, i18n_text(FILTERS[index]));
        describe_filter(screen, index, detail);
        break;
    case DISCOVERY_SORT:
        copy_text(label, ROW_BYTES, i18n_text(SORTS[index]));
        if (index == 6) {
            copy_text(detail, DETAIL_BYTES,
                      i18n_text(screen->query.descending ? STR_DESCENDING : STR_ASCENDING));
        } else if (index == (int)screen->query.sort) {
            copy_text(detail, DETAIL_BYTES, CHECKED);
        }
        break;
    case DISCOVERY_OPTIONS:
        copy_text(label, ROW_BYTES, i18n_text(OPTIONS[index]));
        break;
    case DISCOVERY_ACTIONS:
        copy_text(label, ROW_BYTES, i18n_text(ACTIONS[index]));
        break;
    case DISCOVERY_TAGS:
    case DISCOVERY_TAG_FILTER:
        describe_tag(screen, index, label, detail);
        break;
    case DISCOVERY_TAG_ACTIONS:
        copy_text(label, ROW_BYTES, i18n_text(index == 0 ? STR_RENAME : STR_DELETE));
        break;
    case DISCOVERY_TAG_DELETE:
        copy_text(label, ROW_BYTES, i18n_text(index == 0 ? STR_KEEP_TAG : STR_DELETE));
        break;
    default:
        break;
    }
}

static void refresh_rows(DiscoveryScreen *screen)
{
    if (work.first == screen->list.top && work.cached_mode == screen->mode) {
        return;
    }
    work.first = screen->list.top;
    work.cached_mode = screen->mode;
    int count = row_count(screen);
    for (int row = 0; row < THEME_MAX_ROWS; row++) {
        if (work.first + row < count) {
            describe_row(screen, work.first + row, work.labels[row], work.details[row]);
        } else {
            work.labels[row][0] = work.details[row][0] = 0;
        }
    }
}

static const char *row_label(const void *context, int index)
{
    const DiscoveryScreen *screen = context;
    int row = index - screen->list.top;
    if (screen->mode == DISCOVERY_INFO && index == 8) {
        return work.path;
    }
    return row >= 0 && row < THEME_MAX_ROWS ? work.labels[row] : "";
}

static const char *row_detail(const void *context, int index)
{
    const DiscoveryScreen *screen = context;
    int row = index - screen->list.top;
    return row >= 0 && row < THEME_MAX_ROWS && work.details[row][0] != 0 ? work.details[row] : NULL;
}

static bool row_opens(const void *context, int index)
{
    const DiscoveryScreen *screen = context;
    return screen->mode == DISCOVERY_OPTIONS || screen->mode == DISCOVERY_FILTERS ||
           screen->mode == DISCOVERY_ACTIONS || (screen->mode == DISCOVERY_INFO && index >= 9) ||
           (screen->mode == DISCOVERY_TAGS && (index == 0 || screen->info_track == LIBRARY_NONE));
}

static void remember_selection(DiscoveryScreen *screen)
{
    if (screen->mode == DISCOVERY_RESULTS && work.view.state == LIBRARY_VIEW_READY) {
        screen->selected_track = selected_track(screen);
        screen->play_position = (uint16_t)screen->list.selected;
    }
}

static void results(DiscoveryScreen *screen, bool rebuild)
{
    show_mode(screen, DISCOVERY_RESULTS);
    if (rebuild) {
        begin_view(screen);
    } else {
        list_view_select(&screen->list, screen->play_position, screen->now_ms);
        work.first = -1;
    }
}

static void begin_editor(DiscoveryScreen *screen, uint8_t kind, const char *text)
{
    screen->previous_mode = screen->mode;
    screen->editor_external = false;
    screen->edit_kind = kind;
    discovery_text_begin(&screen->editor, text, kind >= EDIT_MIN_YEAR && kind <= EDIT_MAX_DURATION);
    show_mode(screen, DISCOVERY_EDITOR);
}

static void prepare_info(DiscoveryScreen *screen)
{
    StringBuilder builder = string_builder_make(work.path, sizeof work.path);
    string_builder_append(&builder, i18n_text(STR_FILE_PATH));
    string_builder_append(&builder, ": ");
    append_info(screen, 8, &builder);
}

static void open_info(DiscoveryScreen *screen, uint16_t track)
{
    screen->info_track = track;
    show_mode(screen, DISCOVERY_INFO);
    prepare_info(screen);
}

static void open_tags(DiscoveryScreen *screen)
{
    work.mask_known = screen->info_track == LIBRARY_NONE;
    work.track_mask = 0;
    if (screen->info_track != LIBRARY_NONE && !tags_busy()) {
        if (!personal_tags_mask(work.tag_store, screen->info_track, &work.track_mask)) {
            notice(screen, STR_TAG_SAVE_FAILED, DISCOVERY_INFO);
            return;
        }
        work.mask_known = true;
    }
    show_mode(screen, DISCOVERY_TAGS);
}

static void begin_tag_change(DiscoveryScreen *screen, PersonalTagChange change)
{
    if (!personal_tags_change(work.tag_store, change)) {
        notice(screen, STR_TAG_SAVE_FAILED, DISCOVERY_TAGS);
    } else {
        work.tag_change = true;
        work.mask_known = false;
        work.remove_tag = change.operation == PERSONAL_TAG_DELETE;
    }
}

static void edit_new_tag(DiscoveryScreen *screen)
{
    for (uint8_t tag = 0; tag < PERSONAL_TAG_LIMIT; tag++) {
        if (personal_tags_name(work.tag_store, tag) == NULL) {
            screen->tag = tag;
            begin_editor(screen, EDIT_TAG_CREATE, "");
            return;
        }
    }
    notice(screen, STR_TAG_SAVE_FAILED, DISCOVERY_TAGS);
}

static void choose_filter(DiscoveryScreen *screen, int index)
{
    if (index < 3) {
        screen->picker_kind = (uint8_t)index;
        show_mode(screen, DISCOVERY_PICKER);
    } else if (index == 7) {
        show_mode(screen, DISCOVERY_TAG_FILTER);
    } else {
        char text[16] = "";
        uint32_t value = numeric_filter(screen, index);
        if (value != 0) {
            StringBuilder builder = string_builder_make(text, sizeof text);
            string_builder_append_unsigned(&builder, value);
        }
        begin_editor(screen, (uint8_t)(index - 2), text);
    }
}

static void choose_picker(DiscoveryScreen *screen, int index)
{
    uint16_t id = index == 0 ? LIBRARY_NONE : (uint16_t)(index - 1);
    if (screen->picker_kind == PICK_ARTIST) {
        screen->query.artist = id;
    } else if (screen->picker_kind == PICK_ALBUM) {
        screen->query.album = id;
    } else {
        screen->query.genre = id;
    }
    show_mode(screen, DISCOVERY_FILTERS);
}

static void choose_options(DiscoveryScreen *screen, int index)
{
    if (index == 0) {
        begin_editor(screen, EDIT_QUERY, screen->query.text);
    } else if (index == 1) {
        show_mode(screen, DISCOVERY_SORT);
    } else if (index == 2) {
        show_mode(screen, DISCOVERY_FILTERS);
    } else {
        if (index == 3) {
            BrowseQuery source = screen->query.source;
            screen->query = library_view_defaults();
            screen->query.source = source;
            screen->required_tags = 0;
        } else {
            screen->query.text[0] = 0;
        }
        results(screen, true);
    }
}

static void queue_selected(DiscoveryScreen *screen, bool next)
{
    PlayerUpNextResult result =
        next ? player_play_next(screen->config.player, screen->info_track)
             : player_add_to_up_next(screen->config.player, screen->info_track);
    StringId text = result == PLAYER_UP_NEXT_ADDED
                        ? (next ? STR_PLAYING_NEXT : STR_ADDED_TO_UP_NEXT)
                    : result == PLAYER_UP_NEXT_STARTED ? STR_PLAYING
                    : result == PLAYER_UP_NEXT_WORKING ? STR_PREPARING_STORAGE
                    : result == PLAYER_UP_NEXT_FULL    ? STR_UP_NEXT_FULL
                                                       : STR_QUEUE_SAVE_FAILED;
    notice(screen, text, DISCOVERY_RESULTS);
}

static void choose_tag(DiscoveryScreen *screen, int index)
{
    if (index == 0) {
        if (screen->mode == DISCOVERY_TAG_FILTER) {
            show_mode(screen, DISCOVERY_FILTERS);
        } else {
            edit_new_tag(screen);
        }
        return;
    }
    uint8_t tag = tag_at(index - 1);
    if (tag >= PERSONAL_TAG_LIMIT) {
        return;
    }
    if (screen->mode == DISCOVERY_TAG_FILTER) {
        discovery_preferences_validate_tags(screen, work.tag_store);
        screen->required_tags ^= 1u << tag;
        work.first = -1;
    } else if (screen->info_track == LIBRARY_NONE) {
        screen->tag = tag;
        show_mode(screen, DISCOVERY_TAG_ACTIONS);
    } else {
        begin_tag_change(screen, (PersonalTagChange){.operation = PERSONAL_TAG_ASSIGN,
                                                     .track = screen->info_track,
                                                     .mask = work.track_mask ^ (1u << tag)});
    }
}

static void choose_row(DiscoveryScreen *screen, int index)
{
    switch (screen->mode) {
    case DISCOVERY_RESULTS:
        if (work.view.state == LIBRARY_VIEW_READY && work.view.count != 0 && !busy_view()) {
            screen->play_position = (uint16_t)index;
            screen->request = DISCOVERY_REQUEST_PLAY;
        }
        break;
    case DISCOVERY_OPTIONS:
        choose_options(screen, index);
        break;
    case DISCOVERY_SORT:
        if (index == 6) {
            screen->query.descending = !screen->query.descending;
        } else {
            screen->query.sort = (LibraryViewSort)index;
        }
        results(screen, true);
        break;
    case DISCOVERY_FILTERS:
        choose_filter(screen, index);
        break;
    case DISCOVERY_PICKER:
        choose_picker(screen, index);
        break;
    case DISCOVERY_INFO:
        if (index == 9) {
            open_tags(screen);
        } else if (index == 10) {
            queue_selected(screen, true);
        }
        break;
    case DISCOVERY_TAGS:
    case DISCOVERY_TAG_FILTER:
        choose_tag(screen, index);
        break;
    case DISCOVERY_TAG_ACTIONS:
        if (index == 0) {
            begin_editor(screen, EDIT_TAG_RENAME, personal_tags_name(work.tag_store, screen->tag));
        } else {
            show_mode(screen, DISCOVERY_TAG_DELETE);
        }
        break;
    case DISCOVERY_TAG_DELETE:
        if (index == 0) {
            show_mode(screen, DISCOVERY_TAGS);
        } else {
            begin_tag_change(
                screen, (PersonalTagChange){.operation = PERSONAL_TAG_DELETE, .tag = screen->tag});
        }
        break;
    case DISCOVERY_ACTIONS:
        if (index == 0) {
            open_info(screen, screen->info_track);
        } else if (index == 1) {
            open_tags(screen);
        } else {
            queue_selected(screen, index == 2);
        }
        break;
    default:
        break;
    }
}

static void finish_editor(DiscoveryScreen *screen)
{
    if (screen->edit_kind == EDIT_QUERY) {
        copy_text(screen->query.text, sizeof screen->query.text, screen->editor.text);
        results(screen, true);
        return;
    }
    if (screen->edit_kind >= EDIT_TAG_CREATE) {
        PersonalTagOperation operation =
            screen->edit_kind == EDIT_TAG_CREATE ? PERSONAL_TAG_CREATE : PERSONAL_TAG_RENAME;
        begin_tag_change(screen, (PersonalTagChange){.operation = operation,
                                                     .tag = screen->tag,
                                                     .name = screen->editor.text});
        return;
    }
    uint32_t number;
    bool year = screen->edit_kind <= EDIT_MAX_YEAR;
    if (!discovery_text_number(&screen->editor, year ? UINT16_MAX : UINT32_MAX / 60000, &number)) {
        notice(screen, STR_ENTER_NUMBER, DISCOVERY_FILTERS);
        return;
    }
    if (screen->edit_kind == EDIT_MIN_YEAR) {
        screen->query.year_min = (uint16_t)number;
    } else if (screen->edit_kind == EDIT_MAX_YEAR) {
        screen->query.year_max = (uint16_t)number;
    } else if (screen->edit_kind == EDIT_MIN_DURATION) {
        screen->query.duration_min_ms = number * 60000;
    } else {
        screen->query.duration_max_ms = number * 60000;
    }
    show_mode(screen, DISCOVERY_FILTERS);
}

static ScreenAction back(DiscoveryScreen *screen)
{
    if (screen->request != DISCOVERY_REQUEST_NONE) {
        screen->request = DISCOVERY_REQUEST_CANCEL_PLAY;
    } else if (work.tag_change) {
        personal_tags_cancel(work.tag_store);
        work.tag_change = false;
        show_mode(screen, DISCOVERY_TAGS);
    } else if (screen->mode == DISCOVERY_RESULTS) {
        if (busy_view()) {
            library_view_cancel(&work.view);
        } else {
            return screen_action_pop();
        }
        work.pending_search = work.restoring = false;
    } else if (screen->mode == DISCOVERY_EDITOR) {
        if (screen->editor_external) {
            return screen_action_pop();
        }
        show_mode(screen, (DiscoveryMode)screen->previous_mode);
    } else if (screen->mode == DISCOVERY_INFO && screen->info_external) {
        return screen_action_pop();
    } else if (screen->mode == DISCOVERY_INFO || screen->mode == DISCOVERY_ACTIONS ||
               screen->mode == DISCOVERY_OPTIONS) {
        results(screen, work.view.state != LIBRARY_VIEW_READY);
    } else if (screen->mode == DISCOVERY_TAGS && screen->info_track == LIBRARY_NONE) {
        return screen_action_pop();
    } else if (screen->mode == DISCOVERY_TAGS) {
        show_mode(screen, DISCOVERY_INFO);
        prepare_info(screen);
    } else if (screen->mode == DISCOVERY_NOTICE) {
        if (screen->previous_mode == DISCOVERY_RESULTS) {
            results(screen, false);
        } else {
            show_mode(screen, (DiscoveryMode)screen->previous_mode);
        }
    } else if (screen->mode == DISCOVERY_FILTERS) {
        results(screen, true);
    } else if (screen->mode == DISCOVERY_TAG_ACTIONS || screen->mode == DISCOVERY_TAG_DELETE) {
        show_mode(screen, DISCOVERY_TAGS);
    } else if (screen->mode == DISCOVERY_TAG_FILTER || screen->mode == DISCOVERY_PICKER) {
        show_mode(screen, DISCOVERY_FILTERS);
    } else {
        show_mode(screen, DISCOVERY_OPTIONS);
    }
    return screen_action_none();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    DiscoveryScreen *screen = context;
    ensure_work(screen);
    screen->now_ms = now_ms;
    Navigation navigation = navigation_from_key(event);
    if (navigation.intent == NAVIGATION_HOME) {
        discovery_screen_cancel(screen);
        return screen_action_pop_to_root();
    }
    if (navigation.intent == NAVIGATION_BACK) {
        return back(screen);
    }
    if (screen->request != DISCOVERY_REQUEST_NONE || work.tag_change ||
        (tags_busy() && (screen->mode == DISCOVERY_TAGS || screen->mode == DISCOVERY_TAG_FILTER))) {
        return screen_action_none();
    }
    if (screen->mode == DISCOVERY_EDITOR) {
        if (event.key == KEY_PLAY && event.gesture == KEY_GESTURE_LONG_PRESS) {
            finish_editor(screen);
        } else {
            discovery_text_key(&screen->editor, event);
        }
    } else if (event.key == KEY_MENU && screen->mode == DISCOVERY_RESULTS) {
        remember_selection(screen);
        show_mode(screen, DISCOVERY_OPTIONS);
    } else if (navigation.intent == NAVIGATION_NEXT || navigation.intent == NAVIGATION_PREVIOUS) {
        int direction = navigation.intent == NAVIGATION_NEXT ? 1 : -1;
        if (navigation.repeated) {
            list_view_keep_scrolling(&screen->list, direction, now_ms);
        } else {
            list_view_start_scroll(&screen->list, direction, now_ms);
        }
    } else if (navigation.intent == NAVIGATION_SELECT && row_count(screen) != 0) {
        choose_row(screen, screen->list.selected);
    } else if (navigation.intent == NAVIGATION_OPTIONS) {
        if (screen->mode == DISCOVERY_RESULTS && work.view.state == LIBRARY_VIEW_READY) {
            remember_selection(screen);
            screen->info_track = screen->selected_track;
            show_mode(screen, DISCOVERY_ACTIONS);
        } else if (screen->mode == DISCOVERY_TAGS && screen->list.selected > 0) {
            screen->tag = tag_at(screen->list.selected - 1);
            show_mode(screen, DISCOVERY_TAG_ACTIONS);
        }
    }
    refresh_rows(screen);
    return screen_action_none();
}

static void restore_selection(DiscoveryScreen *screen)
{
    uint16_t tracks[LIBRARY_VIEW_PAGE_TRACKS];
    uint32_t remaining = work.view.count - work.restore_first;
    uint16_t count =
        remaining > LIBRARY_VIEW_PAGE_TRACKS ? LIBRARY_VIEW_PAGE_TRACKS : (uint16_t)remaining;
    if (count == 0 || !library_view_read(&work.view, (uint16_t)work.restore_first, count, tracks)) {
        work.restoring = false;
        return;
    }
    for (uint16_t index = 0; index < count; index++) {
        if (tracks[index] == screen->selected_track) {
            list_view_select(&screen->list, (int)(work.restore_first + index), screen->now_ms);
            work.restoring = false;
            break;
        }
    }
    work.restore_first += count;
    if (work.restore_first == work.view.count) {
        work.restoring = false;
    }
}

static void update_tags(DiscoveryScreen *screen)
{
    bool was_busy = tags_busy();
    if (was_busy) {
        personal_tags_step(work.tag_store);
    }
    if (was_busy && !tags_busy()) {
        work.first = -1;
    }
    if (work.tag_change && !tags_busy()) {
        work.tag_change = false;
        if (work.tag_store->result != STORE_DONE) {
            notice(screen, STR_TAG_SAVE_FAILED, DISCOVERY_TAGS);
        } else {
            if (work.remove_tag) {
                screen->required_tags &= ~(1u << screen->tag);
            }
            show_mode(screen, DISCOVERY_TAGS);
        }
    }
    if (screen->mode == DISCOVERY_TAGS && !tags_busy() && !work.mask_known &&
        screen->info_track != LIBRARY_NONE) {
        if (!personal_tags_mask(work.tag_store, screen->info_track, &work.track_mask)) {
            notice(screen, STR_TAG_SAVE_FAILED, DISCOVERY_INFO);
        } else {
            work.mask_known = true;
        }
        work.first = -1;
    }
}

static bool step_view(DiscoveryScreen *screen)
{
    uint32_t started = hal_clock_microseconds();
    for (unsigned step = 0; step < WORK_STEPS; step++) {
        if (work.view.state != LIBRARY_VIEW_SCANNING && work.view.state != LIBRARY_VIEW_SORTING) {
            break;
        }
        library_view_step(&work.view);
        hal_audio_service();
        if (hal_clock_microseconds() - started >= WORK_BUDGET_US) {
            break;
        }
    }
    if (work.view.state != LIBRARY_VIEW_READY) {
        return false;
    }
    work.restore_first = 0;
    work.restoring = screen->selected_track != LIBRARY_NONE;
    fit(screen);
    work.first = -1;
    return true;
}

static void update(void *context, uint32_t now_ms)
{
    DiscoveryScreen *screen = context;
    bool missing = work.owner != screen;
    ensure_work(screen);
    screen->now_ms = now_ms;
    if (missing && screen->mode == DISCOVERY_RESULTS) {
        begin_view(screen);
    }
    update_tags(screen);
    if (work.pending_search && !tags_busy()) {
        begin_view(screen);
    }
    if (work.view.state == LIBRARY_VIEW_SCANNING || work.view.state == LIBRARY_VIEW_SORTING) {
        step_view(screen);
    }
    if (work.restoring) {
        restore_selection(screen);
    }
    refresh_rows(screen);
    hal_audio_service();
}

static void enter(void *context, uint32_t now_ms)
{
    DiscoveryScreen *screen = context;
    bool missing = work.owner != screen;
    screen->now_ms = now_ms;
    ensure_work(screen);
    if (missing && screen->mode == DISCOVERY_RESULTS) {
        begin_view(screen);
    } else if (missing && screen->mode == DISCOVERY_INFO) {
        prepare_info(screen);
    }
    fit(screen);
    refresh_rows(screen);
}

static void draw_line(Canvas *canvas, const Theme *theme, Rect area, const char *text, bool small)
{
    TextStyle style = {.font = small ? theme->fonts.small : theme->fonts.body,
                       .color = small ? theme->colors.text_secondary : theme->colors.text};
    label_draw(canvas, style, (Label){area, text});
}

static void draw_editor(const DiscoveryScreen *screen, Canvas *canvas, Rect area)
{
    const Theme *theme = screen->config.theme;
    area.x += theme->metrics.padding;
    area.width -= 2 * theme->metrics.padding;
    area.y += theme->metrics.padding;
    area.height = theme->metrics.row_height;
    draw_line(canvas, theme, area,
              discovery_text_tail(screen->editor.text, theme->fonts.body, area.width), false);
    char selected[] = {'[', ' ', ' ', ' ', ']', 0};
    selected[2] = discovery_text_character(&screen->editor);
    area.y += area.height;
    draw_line(canvas, theme, area, selected, false);
    area.y += area.height;
    area.height = font_line_height(theme->fonts.small) + 2;
    const StringId hints[] = {STR_QUERY_CHOOSE_HINT, STR_QUERY_ADD_HINT, STR_QUERY_ERASE_HINT,
                              screen->edit_kind == EDIT_QUERY ? STR_QUERY_SEARCH_HINT
                                                              : STR_SAVE_HINT};
    for (unsigned index = 0; index < sizeof hints / sizeof *hints; index++) {
        draw_line(canvas, theme, area, i18n_text(hints[index]), true);
        area.y += area.height;
    }
}

static const char *state_message(void)
{
    if (work.view.state == LIBRARY_VIEW_CANCELLED) {
        return i18n_text(STR_SEARCH_CANCELLED);
    }
    if (busy_view()) {
        return i18n_text(STR_SEARCHING_MUSIC);
    }
    return i18n_text(work.view.state == LIBRARY_VIEW_READY ? STR_NO_MATCHES : STR_SEARCH_FAILED);
}

static bool filtered(const DiscoveryScreen *screen)
{
    return screen->query.artist != LIBRARY_NONE || screen->query.album != LIBRARY_NONE ||
           screen->query.genre != LIBRARY_NONE || screen->query.year_min != 0 ||
           screen->query.year_max != 0 || screen->query.duration_min_ms != 0 ||
           screen->query.duration_max_ms != 0 || screen->required_tags != 0;
}

static void draw_summary(const DiscoveryScreen *screen, Canvas *canvas, Rect area)
{
    const Theme *theme = screen->config.theme;
    char text[128];
    StringBuilder builder = string_builder_make(text, sizeof text);
    if (work.view.state == LIBRARY_VIEW_READY) {
        string_builder_append_unsigned(&builder, work.view.count);
    } else {
        string_builder_append_unsigned(&builder, work.view.scanned);
        string_builder_append_char(&builder, '/');
        string_builder_append_unsigned(&builder, work.view.total);
    }
    string_builder_append_char(&builder, ' ');
    const char *scope =
        screen->config.scope_title != NULL ? screen->config.scope_title : i18n_text(STR_ALL_SONGS);
    string_builder_append(&builder, screen->query.text[0] != 0 ? screen->query.text : scope);
    area.x += theme->metrics.padding;
    area.width -= 2 * theme->metrics.padding;
    area.height = SUMMARY_HEIGHT;
    if (filtered(screen)) {
        const char *active = i18n_text(STR_FILTER_ACTIVE);
        int width = text_width(theme->fonts.small, active);
        Rect badge = {area.x + area.width - width, area.y, width, area.height};
        draw_line(canvas, theme, badge, active, true);
        area.width -= width + theme->metrics.padding;
    }
    draw_line(canvas, theme, area, text, true);
}

static void draw(void *context, Canvas *canvas)
{
    DiscoveryScreen *screen = context;
    const Theme *theme = screen->config.theme;
    Rect area = theme_content_area(theme);
    canvas_fill_rect(canvas, area, theme->colors.background);
    header_draw(canvas, theme, screen->screen.title);
    if (screen->mode == DISCOVERY_EDITOR) {
        draw_editor(screen, canvas, area);
        return;
    }
    if (screen->request != DISCOVERY_REQUEST_NONE || work.tag_change) {
        message_draw(canvas, theme, area,
                     i18n_text(work.tag_change ? STR_SAVING_TAGS : STR_DISCOVERY_PREPARING));
        return;
    }
    if (screen->mode == DISCOVERY_NOTICE) {
        message_draw(canvas, theme, area, i18n_text(work.notice));
        return;
    }
    if (screen->mode == DISCOVERY_RESULTS) {
        draw_summary(screen, canvas, area);
        if (busy_view() || work.view.state != LIBRARY_VIEW_READY || work.view.count == 0) {
            message_draw(canvas, theme, list_area(screen), state_message());
        } else {
            list_view_draw(&screen->list, canvas, list_area(screen), screen->now_ms);
        }
        area.y += area.height - FOOTER_HEIGHT;
        area.height = FOOTER_HEIGHT;
        area.x += theme->metrics.padding;
        area.width -= 2 * theme->metrics.padding;
        draw_line(canvas, theme, area,
                  i18n_text(work.pref_failed ? STR_VIEW_SAVE_FAILED : STR_DISCOVERY_HINT), true);
        return;
    }
    if ((screen->mode == DISCOVERY_TAGS || screen->mode == DISCOVERY_TAG_FILTER) && tags_busy()) {
        message_draw(canvas, theme, area, i18n_text(STR_LOADING_TAGS));
    } else {
        list_view_draw(&screen->list, canvas, area, screen->now_ms);
    }
}

Screen *discovery_screen_init(DiscoveryScreen *screen, DiscoveryScreenConfig config)
{
    *screen = (DiscoveryScreen){
        .config = config, .selected_track = LIBRARY_NONE, .info_track = LIBRARY_NONE};
    screen->query = library_view_defaults();
    if (work.owner == screen) {
        work.owner = NULL;
    }
    screen->model = (ListModel){.context = screen,
                                .count = row_count,
                                .label = row_label,
                                .detail = row_detail,
                                .opens = row_opens,
                                .detail_below = true};
    screen->screen = (Screen){.title = i18n_text(STR_SEARCH_RESULTS),
                              .context = screen,
                              .takes_every_key = true,
                              .stays_on_wake = true,
                              .enter = enter,
                              .handle_key = handle_key,
                              .update = update,
                              .draw = draw};
    ensure_work(screen);
    screen->initialized = true;
    show_mode(screen, DISCOVERY_RESULTS);
    return &screen->screen;
}

Screen *discovery_screen_open(DiscoveryScreen *screen, BrowseQuery source, bool edit_search)
{
    ensure_work(screen);
    screen->query.source = source;
    screen->info_external = false;
    screen->auto_play = false;
    screen->request = DISCOVERY_REQUEST_NONE;
    screen->destination = NULL;
    show_mode(screen, DISCOVERY_RESULTS);
    if (edit_search) {
        begin_editor(screen, EDIT_QUERY, screen->query.text);
        screen->editor_external = true;
    } else {
        begin_view(screen);
    }
    screen->initialized = true;
    return &screen->screen;
}

Screen *discovery_screen_options(DiscoveryScreen *screen, BrowseQuery source)
{
    discovery_screen_open(screen, source, false);
    show_mode(screen, DISCOVERY_OPTIONS);
    return &screen->screen;
}

Screen *discovery_screen_info(DiscoveryScreen *screen, uint16_t track)
{
    ensure_work(screen);
    screen->info_external = true;
    open_info(screen, track);
    return &screen->screen;
}

Screen *discovery_screen_tags(DiscoveryScreen *screen, uint16_t track)
{
    discovery_screen_info(screen, track);
    open_tags(screen);
    return &screen->screen;
}

TrackOrderSource discovery_screen_order(DiscoveryScreen *screen)
{
    if (work.owner != screen || work.view.state != LIBRARY_VIEW_READY || busy_view()) {
        return (TrackOrderSource){0};
    }
    return (TrackOrderSource){library_view_source_read, &work.view, work.view.count};
}

void discovery_screen_playback_done(DiscoveryScreen *screen, bool success)
{
    bool cancelled = screen->request == DISCOVERY_REQUEST_CANCEL_PLAY;
    screen->request = DISCOVERY_REQUEST_NONE;
    if (!success && !cancelled) {
        notice(screen, STR_QUEUE_SAVE_FAILED, DISCOVERY_RESULTS);
    }
}

void discovery_screen_cancel(DiscoveryScreen *screen)
{
    if (work.owner == screen) {
        if (busy_view()) {
            library_view_cancel(&work.view);
        }
        if (work.tag_change) {
            personal_tags_cancel(work.tag_store);
        }
        work.pending_search = work.restoring = work.tag_change = false;
    }
    if (screen->request != DISCOVERY_REQUEST_NONE) {
        screen->request = DISCOVERY_REQUEST_CANCEL_PLAY;
    }
    screen->destination = NULL;
}

LibraryViewState discovery_screen_state(const DiscoveryScreen *screen)
{
    return work.owner != screen  ? LIBRARY_VIEW_IDLE
           : work.pending_search ? LIBRARY_VIEW_SCANNING
                                 : work.view.state;
}
