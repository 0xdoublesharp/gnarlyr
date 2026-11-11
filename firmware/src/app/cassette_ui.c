#include "mixtape_ui_private.h"

#include "core/i18n.h"
#include "core/string_builder.h"
#include "playback/cassette.h"

Screen *mixtape_ui_add(MixtapeUi *ui, uint16_t track)
{
    if (mixtape_ui_busy(ui) ||
        mix_identity_read(ui->config.player->library, track, &ui->add_identity) != STORE_READY) {
        return &ui->screen;
    }
    ui->add_track = track;
    ui->adding = true;
    ui->add_requested = true;
    ui->renaming = false;
    ui->save_queue = false;
    ui->appending = false;
    ui->mode = MIXTAPE_PICK;
    mixtape_ui_refresh(ui);
    return &ui->screen;
}

static bool read_append(void *context, uint16_t index, MixIdentity *identity)
{
    MixtapeUi *ui = context;
    if (index == 0) {
        mix_tape_cursor(&ui->append_cursor, &ui->append_tape);
    }
    uint16_t insertion = ui->selected_side == 0 ? ui->side_a_count : ui->append_tape.count;
    if (index == insertion) {
        *identity = ui->add_identity;
        return true;
    }
    return mix_tape_next(&ui->append_cursor, identity) == STORE_READY;
}

static bool durations_fit(const MixtapeUi *ui)
{
    uint32_t used[2] = {0};
    const Library *library = ui->config.player->library;
    for (uint16_t index = 0; index < ui->resolver.count; index++) {
        uint8_t side = index < ui->side_a_count ? 0 : 1;
        LibraryTrack track;
        if (!library_read_track(library, ui->resolver.tracks[index], &track) ||
            !cassette_budget(ui->side_minutes, used[side], track.duration_ms)) {
            return false;
        }
        used[side] += track.duration_ms;
    }
    LibraryTrack added;
    return library_read_track(library, ui->add_track, &added) &&
           cassette_budget(ui->side_minutes, used[ui->selected_side], added.duration_ms);
}

bool mixtape_ui_append(MixtapeUi *ui)
{
    uint16_t side_count = ui->selected_side == 0
                              ? ui->side_a_count
                              : (uint16_t)(ui->store.tape.count - ui->side_a_count);
    if (ui->resolver.missing != 0 || side_count >= CASSETTE_SIDE_CAPACITY || !durations_fit(ui)) {
        return false;
    }
    ui->append_file = ui->store.data_file;
    ui->append_tape = ui->store.tape;
    ui->append_tape.file.source.context = &ui->append_file;
    MixSource source = {read_append, ui, (uint16_t)(ui->append_tape.count + 1)};
    MixtapeChange change = {.operation = MIXTAPE_SAVE,
                            .slot = ui->slot,
                            .name = ui->name,
                            .source = source,
                            .side_minutes = ui->side_minutes,
                            .side_a_count =
                                (uint16_t)(ui->side_a_count + (ui->selected_side == 0))};
    if (!mixtape_store_change(&ui->store, change)) {
        return false;
    }
    ui->appending = true;
    ui->stage = MIXTAPE_STORE;
    return true;
}
