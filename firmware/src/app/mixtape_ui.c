#include "mixtape_ui_private.h"

#include <stddef.h>

#include "core/i18n.h"

#include "hal/clock.h"
#include "hal/system.h"
#include "library/spoken_path.h"
#include "player_private.h"

static const MixtapeUiApi API = {.init = mixtape_ui_init,
                                 .busy = mixtape_ui_busy,
                                 .step = mixtape_ui_step,
                                 .cancel = mixtape_ui_cancel};

const MixtapeUiApi *mixtape_ui_api(void)
{
    return &API;
}

bool mixtape_ui_busy(const MixtapeUi *ui)
{
    return ui->stage != 0;
}

void mixtape_ui_message(MixtapeUi *ui, const char *text)
{
    toast_show(ui->config.toast, text, ui->now_ms);
}

static void return_scratch(MixtapeUi *ui)
{
    if (ui->borrowed) {
        hal_system_return_scratch();
        ui->borrowed = false;
    }
}

static void finish(MixtapeUi *ui, const char *message)
{
    ui->destination = NULL;
    return_scratch(ui);
    ui->stage = 0;
    if (ui->resume && ui->config.player->state == PLAYER_PAUSED) {
        player_toggle_pause(ui->config.player);
    }
    ui->resume = false;
    mixtape_ui_refresh(ui);
    mixtape_ui_message(ui, message);
}

void mixtape_ui_cancel(MixtapeUi *ui)
{
    ui->destination = NULL;
    if (mixtape_ui_busy(ui)) {
        player_queue_cancel(ui->config.player);
        mixtape_store_cancel(&ui->store);
        track_order_job_abort(&ui->writer);
        finish(ui, i18n_text(STR_CANCELLED));
    }
}

static bool read_queue(void *context, uint16_t index, MixIdentity *identity)
{
    const Player *player = context;
    uint16_t track = 0;
    return track_order_read(&player->storage.order, index, 1, &track) &&
           mix_identity_read(player->library, track, identity) == STORE_READY;
}

static bool begin_save(MixtapeUi *ui)
{
    Player *player = ui->config.player;
    MixSource source = ui->save_queue ? (MixSource){read_queue, player, player->storage.order.count}
                                      : (MixSource){0};
    MixtapeChange change = {.operation = ui->renaming ? MIXTAPE_RENAME : MIXTAPE_SAVE,
                            .slot = ui->slot,
                            .name = ui->name,
                            .source = source};
    if (!mixtape_store_change(&ui->store, change)) {
        return false;
    }
    ui->stage = MIXTAPE_STORE;
    return true;
}

bool mixtape_ui_save(MixtapeUi *ui)
{
    ui->destination = NULL;
    Player *player = ui->config.player;
    if (!ui->save_queue || ui->renaming) {
        return begin_save(ui);
    }
    ui->resume = player->state == PLAYER_PLAYING;
    if (player_snapshot_queue(player) != PLAYER_QUEUE_EDIT_WORKING) {
        ui->resume = false;
        return false;
    }
    ui->stage = MIXTAPE_SNAPSHOT;
    return true;
}

static void snapshot(MixtapeUi *ui)
{
    Player *player = ui->config.player;
    player_update(player, ui->now_ms);
    if (player_queue_busy(player)) {
        return;
    }
    if (player->editing.result != PLAYER_QUEUE_EDITED) {
        finish(ui, i18n_text(STR_QUEUE_PREPARE_FAILED));
        return;
    }
    if (player->state == PLAYER_PLAYING) {
        player_toggle_pause(player);
    }
    if (!begin_save(ui)) {
        finish(ui, i18n_text(STR_MIXTAPE_SAVE_FAILED));
    }
}

bool mixtape_ui_load(MixtapeUi *ui)
{
    ui->destination = NULL;
    Player *player = ui->config.player;
    if (!mixtape_store_open(&ui->store, ui->slot)) {
        return false;
    }
    ui->resume = player->state == PLAYER_PLAYING;
    if (ui->resume) {
        player_toggle_pause(player);
    }
    ui->stage = MIXTAPE_OPEN;
    return true;
}

static bool begin_resolve(MixtapeUi *ui)
{
    Player *player = ui->config.player;
    if (ui->store.tape.count == 0 || (player->has_track && !player_save(player))) {
        return false;
    }
    player_release_output(player);
    uint32_t size = 0;
    void *memory = hal_system_borrow_scratch(&size);
    ui->borrowed = memory != NULL;
    return ui->borrowed && mix_resolve_begin(&ui->resolver, player->library, &ui->store.tape,
                                             (MixMemory){memory, size});
}

static void open_tape(MixtapeUi *ui)
{
    StoreResult result = mixtape_store_step(&ui->store);
    if (result == STORE_DONE) {
        if (begin_resolve(ui)) {
            ui->stage = MIXTAPE_RESOLVE;
        } else {
            finish(ui, ui->store.tape.count == 0 ? i18n_text(STR_MIXTAPE_EMPTY)
                                                 : i18n_text(STR_MIXTAPE_OPEN_FAILED));
        }
    } else if (result != STORE_WORKING) {
        finish(ui, i18n_text(STR_MIXTAPE_READ_FAILED));
    }
}

static bool session_writable(Player *player)
{
    HalFile file;
    if (!hal_file_open(&file, PLAYER_SESSION_PATH)) {
        return true;
    }
    hal_file_close(&file);
    return !player->storage.journal.needs_reload;
}

static bool begin_copy(MixtapeUi *ui)
{
    Player *player = ui->config.player;
    if (ui->resolver.count == 0 || !session_writable(player)) {
        return false;
    }
    StoreLoad load = STORE_READY;
    if (player->queue.stored_order != NULL) {
        ui->pending = player->storage.order;
    } else {
        load = player_storage_order(&player->storage, &ui->pending, NULL);
    }
    TrackOrderSource source = mix_resolve_source(&ui->resolver);
    return (load == STORE_READY || load == STORE_EMPTY) &&
           track_order_job_begin(&ui->writer, &ui->pending, source,
                                 (TrackOrderEdit){.operation = TRACK_ORDER_COPY});
}

static void resolve(MixtapeUi *ui)
{
    StoreResult result = mix_resolve_step(&ui->resolver);
    if (result == STORE_DONE) {
        if (begin_copy(ui)) {
            ui->stage = MIXTAPE_COPY;
        } else {
            finish(ui, ui->resolver.count == 0 ? i18n_text(STR_SONGS_UNAVAILABLE)
                                               : i18n_text(STR_QUEUE_SAVE_FAILED));
        }
    } else if (result != STORE_WORKING) {
        finish(ui, i18n_text(STR_SONGS_NOT_FOUND));
    }
}

static bool publish(MixtapeUi *ui)
{
    Player *player = ui->config.player;
    uint16_t track = 0;
    MixIdentity identity;
    if (!track_order_read(&ui->pending, 0, 1, &track) ||
        mix_identity_read(player->library, track, &identity) != STORE_READY) {
        return false;
    }
    SessionState state = player_session_state(player, &player->up_next);
    state.queue_kind = SESSION_QUEUE_STORED;
    state.order_generation = ui->pending.generation;
    state.resume.plan = (PlayQueuePlan){.source = browse_root_query(BROWSE_ALL_SONGS),
                                        .anchor_track = track,
                                        .shuffle = player->shuffle};
    state.resume.cursor = (PlayQueueCursor){0};
    state.resume.track = track;
    state.resume.position_ms = 0;
    state.resume.from_up_next = false;
    up_next_clear(&state.up_next);
    if (!player_store_session(player, &state)) {
        return false;
    }
    player->storage.order = ui->pending;
    play_queue_use_order(&player->queue, player->library, &player->storage.order,
                         state.resume.plan);
    up_next_clear(&player->up_next);
    player->from_up_next = false;
    player->has_track = true;
    player->loaded = false;
    player->has_next = false;
    player->track = track;
    player->start_ms = identity.start_ms;
    player->paused_position_ms = 0;
    player->spoken = spoken_kind_of(identity.path) != SPOKEN_NONE;
    player->state = PLAYER_PAUSED;
    player->up_next_revision++;
    return true;
}

static void copy_queue(MixtapeUi *ui)
{
    StoreResult result = track_order_job_step(&ui->writer);
    if (result == STORE_DONE) {
        if (publish(ui)) {
            ui->resume = ui->loading;
            ui->mode = MIXTAPE_LIST;
            finish(ui, ui->resolver.missing == 0 ? i18n_text(STR_MIXTAPE_LOADED)
                                                 : i18n_text(STR_MIXTAPE_PARTIAL));
            ui->destination = ui->loading ? ui->config.now_playing : ui->config.up_next;
        } else {
            finish(ui, i18n_text(STR_QUEUE_SAVE_FAILED));
        }
    } else if (result != STORE_WORKING) {
        finish(ui, i18n_text(STR_QUEUE_SAVE_FAILED));
    }
}

void mixtape_ui_step(MixtapeUi *ui, uint32_t now_ms)
{
    ui->now_ms = now_ms;
    uint32_t started = hal_clock_microseconds();
    for (int work = 0; work < 16 && mixtape_ui_busy(ui); work++) {
        switch (ui->stage) {
        case MIXTAPE_SNAPSHOT:
            snapshot(ui);
            break;
        case MIXTAPE_STORE: {
            StoreResult result = mixtape_store_step(&ui->store);
            if (result != STORE_WORKING) {
                const char *message = ui->mode == MIXTAPE_CONFIRM_DELETE
                                          ? i18n_text(STR_MIXTAPE_DELETED)
                                      : ui->renaming ? i18n_text(STR_MIXTAPE_RENAMED)
                                                     : i18n_text(STR_MIXTAPE_SAVED);
                if (result == STORE_DONE) {
                    ui->mode = MIXTAPE_LIST;
                }
                finish(ui, result == STORE_DONE ? message : i18n_text(STR_MIXTAPE_SAVE_FAILED));
            }
            break;
        }
        case MIXTAPE_OPEN:
            open_tape(ui);
            break;
        case MIXTAPE_RESOLVE:
            resolve(ui);
            break;
        case MIXTAPE_COPY:
            copy_queue(ui);
            break;
        }
        if (hal_clock_microseconds() - started >= 8000) {
            break;
        }
    }
}
