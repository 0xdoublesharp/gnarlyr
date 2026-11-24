#include <string.h>

#include "app/app.h"
#include "board.h"
#include "check.h"
#include "device/slot_copy.h"
#include "favorites_store.h"
#include "hal/storage.h"
#include "headphone_store.h"
#include "internal_volume.h"
#include "overlay.h"
#include "player.h"
#include "settings_store.h"
#include "spoken_store.h"
#include "storage_reset.h"
#include "trial_module.h"
#include "usb_session.h"
#include "user_state_fat.h"

enum {
    SLOT_1 = 16,
    SLOT_2 = 16384,
    MIRROR = 0x44C000 / FAT_SECTOR_SIZE
};

typedef enum Fault {
    NO_FAULT,
    MOUNT,
    FIND_RECORD,
    READ_RECORD,
    WRITE_RECORD,
    CLEAR_RECORD,
    SLOT_HEADER,
    SLOT_MIRROR,
    RUNNING_HEADER,
    COPY_SLOT,
    SETTLE_USER,
    SETTLE_MOUNT,
    DELETE_RECORD
} Fault;

static struct {
    Fault fault;
    bool injected;
    bool present;
    bool valid[2];
    uint32_t revisions[2];
    uint32_t copies;
    uint32_t restarts;
    uint32_t settles;
    bool settled_change;
    uint8_t bytes[UPDATE_TRIAL_FILE_BYTES];
} card;

static FatVolume volume;
static UsbShared shared;
static uint32_t buffer[SLOT_COPY_BUFFER_WORDS];
static const uint32_t RUNNING_HEADER_WORDS[OVERLAY_HEADER_BYTES / 4] = {0xFE, 1, 2, 3};

UsbShared *usb_session_shared(void)
{
    return &shared;
}

static bool fails(Fault operation)
{
    if (card.fault != operation) {
        return false;
    }
    card.fault = NO_FAULT;
    card.injected = true;
    return true;
}

static void start(UpdateTrial trial, Fault fault)
{
    memset(&card, 0, sizeof card);
    memset(&shared, 0, sizeof shared);
    card.fault = fault;
    card.present = true;
    card.valid[0] = true;
    card.valid[1] = true;
    card.revisions[0] = 2;
    card.revisions[1] = 1;
    update_trial_encode(trial, card.bytes);
}

static void expect_record(UpdateTrialState state, uint32_t attempts)
{
    UpdateTrial found =
        card.present ? update_trial_decode(card.bytes) : (UpdateTrial){UPDATE_TRIAL_NONE, 0};
    CHECK_EQUAL(state, found.state);
    CHECK_EQUAL(attempts, found.attempts);
}

FatVolume *internal_volume_mount(void)
{
    return fails(MOUNT) || (card.settles != 0 && fails(SETTLE_MOUNT)) ? NULL : &volume;
}

static bool read_slot(void *context, uint32_t lba, uint32_t count, void *out)
{
    (void)context;
    CHECK_EQUAL(1, count);
    uint32_t slot = lba < SLOT_2 ? SLOT_1 : SLOT_2;
    uint32_t offset = lba - slot;
    memset(out, 0, FAT_SECTOR_SIZE);
    if (offset == USB_OVERLAY_IMAGE_OFFSET / FAT_SECTOR_SIZE) {
        if (fails(RUNNING_HEADER)) {
            return false;
        }
        memcpy(out, RUNNING_HEADER_WORDS, sizeof RUNNING_HEADER_WORDS);
        return true;
    }
    CHECK(offset == 0 || offset == MIRROR);
    if (fails(offset == 0 ? SLOT_HEADER : SLOT_MIRROR)) {
        return false;
    }
    if (card.valid[slot == SLOT_2]) {
        static const uint8_t HEADER[] = {0, 0xC0, 0x44, 0, 'R', 'K', 'n', 'a', 'n', 'o', 'F', 'W'};
        memcpy((uint8_t *)out + 0x1F4, HEADER, sizeof HEADER);
    }
    return true;
}

bool trial_record_open(TrialDisk *disk)
{
    disk->volume = internal_volume_mount();
    disk->storage = (UpdateStorage){.read = read_slot, .slot_1 = SLOT_1, .slot_2 = SLOT_2};
    return disk->volume != NULL;
}

bool trial_record_store(FatVolume *mounted, const uint8_t *bytes)
{
    CHECK(mounted == &volume);
    UpdateTrial trial = update_trial_decode(bytes);
    if (fails(trial.state == UPDATE_TRIAL_NONE ? CLEAR_RECORD : WRITE_RECORD)) {
        return false;
    }
    memcpy(card.bytes, bytes, UPDATE_TRIAL_BYTES);
    card.present = true;
    return true;
}

bool trial_record_copy(const TrialDisk *disk, uint32_t from, uint32_t to, uint32_t *words,
                       UpdateProgress progress)
{
    (void)disk;
    (void)words;
    (void)progress;
    CHECK(from != to);
    if (fails(COPY_SLOT)) {
        card.valid[to == SLOT_2] = false;
        return false;
    }
    card.valid[to == SLOT_2] = true;
    card.revisions[to == SLOT_2] = card.revisions[from == SLOT_2];
    card.copies++;
    return true;
}

bool fat_open(FatVolume *mounted, const char *path, FatFile *file)
{
    CHECK(mounted == &volume);
    *file = (FatFile){.size = sizeof card.bytes};
    return strcmp(path, UPDATE_TRIAL_PATH) == 0 && !fails(FIND_RECORD) && card.present;
}

bool fat_read(FatVolume *mounted, FatFile *file, uint32_t offset, void *out, uint32_t length)
{
    (void)file;
    CHECK(mounted == &volume);
    CHECK(offset == 0 && length <= sizeof card.bytes);
    if (length == UPDATE_TRIAL_BYTES && fails(READ_RECORD)) {
        return false;
    }
    memcpy(out, card.bytes, length);
    return true;
}

bool fat_write(FatVolume *mounted, FatFile *file, uint32_t offset, const void *bytes,
               uint32_t length)
{
    (void)mounted;
    (void)file;
    (void)offset;
    (void)bytes;
    (void)length;
    return false;
}

FatReserveResult fat_reserve(FatVolume *mounted, const char *path, uint32_t size)
{
    (void)mounted;
    (void)path;
    (void)size;
    return FAT_RESERVE_FAILED;
}

bool fat_delete(FatVolume *mounted, const char *path)
{
    CHECK(mounted == &volume);
    CHECK(strcmp(path, UPDATE_TRIAL_PATH) == 0);
    if (fails(DELETE_RECORD)) {
        return false;
    }
    card.present = false;
    return true;
}

bool fat_fix_label(FatVolume *mounted, const char *target, const char *legacy)
{
    (void)mounted;
    (void)target;
    (void)legacy;
    return false;
}

bool user_state_run(const UserStateIo *io, const char *build, bool changed)
{
    (void)io;
    (void)build;
    card.settles++;
    if (fails(SETTLE_USER)) {
        return false;
    }
    card.settled_change |= changed;
    return true;
}

UserStateIo user_state_fat_io(UserStateDisk *disk, uint8_t *bytes, uint32_t size)
{
    (void)disk;
    (void)bytes;
    (void)size;
    return (UserStateIo){0};
}

const char *hal_system_version(void)
{
    return "test";
}

HalVolume hal_file_volume(void)
{
    return HAL_VOLUME_INTERNAL;
}

FatVolume *board_music_volume(void)
{
    return &volume;
}

void app_show_notice(const char *title, const char *message)
{
    (void)title;
    (void)message;
}

void board_display_service(void)
{
}

void board_wait_for_key(void)
{
}

void board_restart_clean(void)
{
    card.restarts++;
}

void headphone_store_load(HeadphoneStore *store)
{
    (void)store;
}

void settings_store_read(SettingsStore *store)
{
    (void)store;
}

bool player_restore(Player *player, bool cold_start)
{
    (void)player;
    (void)cold_start;
    return false;
}

StoreLoad favorites_store_read(Favorites *favorites)
{
    (void)favorites;
    return STORE_UNSUPPORTED;
}

StoreLoad spoken_store_read(Bookmarks *bookmarks)
{
    (void)bookmarks;
    return STORE_UNSUPPORTED;
}

static void boot_retries_a_transient_storage_failure(void)
{
    static const Fault FAULTS[] = {MOUNT, FIND_RECORD, READ_RECORD, WRITE_RECORD};
    for (size_t index = 0; index < TEST_CASE_COUNT(FAULTS); index++) {
        start((UpdateTrial){UPDATE_TRIAL_RUNNING, 1}, FAULTS[index]);
        TRIAL_BOOT_MODULE_ENTRY.start(&shared, buffer);
        CHECK(card.injected);
        CHECK(!shared.trial.running);
        expect_record(UPDATE_TRIAL_RUNNING, 1);
        TRIAL_BOOT_MODULE_ENTRY.start(&shared, buffer);
        CHECK(shared.trial.running);
        CHECK_EQUAL(USB_SETTLE_CHANGED, shared.trial.settle);
        expect_record(UPDATE_TRIAL_RUNNING, 2);
    }
}

static void rollback_retries_a_transient_storage_failure(void)
{
    static const Fault FAULTS[] = {MOUNT,       FIND_RECORD, READ_RECORD, SLOT_HEADER,
                                   SLOT_MIRROR, COPY_SLOT,   WRITE_RECORD};
    for (size_t index = 0; index < TEST_CASE_COUNT(FAULTS); index++) {
        start((UpdateTrial){UPDATE_TRIAL_RUNNING, UPDATE_TRIAL_LIMIT}, FAULTS[index]);
        TRIAL_BOOT_MODULE_ENTRY.start(&shared, buffer);
        CHECK(card.injected);
        CHECK_EQUAL(0, card.restarts);
        expect_record(UPDATE_TRIAL_RUNNING, UPDATE_TRIAL_LIMIT);
        TRIAL_BOOT_MODULE_ENTRY.start(&shared, buffer);
        CHECK_EQUAL(1, card.restarts);
        CHECK_EQUAL(1, card.revisions[0]);
        expect_record(UPDATE_TRIAL_ROLLED_BACK, 0);
    }
}

static void promotion_retries_a_transient_storage_failure(void)
{
    static const Fault FAULTS[] = {MOUNT,       FIND_RECORD,    READ_RECORD, SLOT_HEADER,
                                   SLOT_MIRROR, RUNNING_HEADER, COPY_SLOT,   WRITE_RECORD};
    for (size_t index = 0; index < TEST_CASE_COUNT(FAULTS); index++) {
        start((UpdateTrial){UPDATE_TRIAL_RUNNING, 1}, FAULTS[index]);
        shared.trial.running = true;
        shared.last_session.end = HAL_USB_END_EJECTED;
        shared.last_session.internal_write_ok = true;
        TRIAL_BOOT_MODULE_ENTRY.session_end(&shared, buffer, RUNNING_HEADER_WORDS);
        CHECK(card.injected);
        CHECK(shared.trial.running || shared.trial.promoting);
        CHECK_EQUAL(1, card.revisions[1]);
        TRIAL_BOOT_MODULE_ENTRY.session_end(&shared, buffer, RUNNING_HEADER_WORDS);
        CHECK(!shared.trial.running && !shared.trial.promoting);
        CHECK_EQUAL(2, card.revisions[1]);
        expect_record(UPDATE_TRIAL_NONE, 0);
    }
}

static void promotion_can_resume_after_its_final_record_write_fails(void)
{
    start((UpdateTrial){UPDATE_TRIAL_RUNNING, 1}, CLEAR_RECORD);
    shared.trial.running = true;
    shared.last_session.end = HAL_USB_END_EJECTED;
    shared.last_session.internal_write_ok = true;
    TRIAL_BOOT_MODULE_ENTRY.session_end(&shared, buffer, RUNNING_HEADER_WORDS);
    CHECK(card.injected && shared.trial.promoting);
    CHECK_EQUAL(2, card.revisions[1]);
    expect_record(UPDATE_TRIAL_PROMOTING, 0);

    shared.last_session = (HalUsbLast){0};
    TRIAL_BOOT_MODULE_ENTRY.session_end(&shared, buffer, RUNNING_HEADER_WORDS);
    CHECK(!shared.trial.running && !shared.trial.promoting);
    CHECK_EQUAL(2, card.revisions[1]);
    expect_record(UPDATE_TRIAL_NONE, 0);
}

static void failed_cleanup_keeps_a_terminal_record(void)
{
    start((UpdateTrial){UPDATE_TRIAL_ROLLED_BACK, 0}, DELETE_RECORD);
    shared.trial.settle = USB_SETTLE_CHANGED | USB_SETTLE_ROLLED_BACK;
    CHECK(TRIAL_BOOT_MODULE_ENTRY.settle(&shared));
    CHECK(card.injected && card.present && card.settled_change);
    expect_record(UPDATE_TRIAL_NONE, 0);
    TRIAL_BOOT_MODULE_ENTRY.start(&shared, buffer);
    CHECK(!card.present);
}

static void failed_settle_keeps_work_pending(void)
{
    static const Fault FAULTS[] = {MOUNT,       SETTLE_USER, SETTLE_MOUNT,
                                   FIND_RECORD, READ_RECORD, CLEAR_RECORD};
    for (size_t index = 0; index < TEST_CASE_COUNT(FAULTS); index++) {
        start((UpdateTrial){UPDATE_TRIAL_ROLLED_BACK, 0}, FAULTS[index]);
        shared.trial.settle = USB_SETTLE_CHANGED | USB_SETTLE_ROLLED_BACK;
        CHECK(!TRIAL_BOOT_MODULE_ENTRY.settle(&shared));
        CHECK(card.injected);
        CHECK((shared.trial.settle & USB_SETTLE_ROLLED_BACK) != 0);
        CHECK(TRIAL_BOOT_MODULE_ENTRY.settle(&shared));
        CHECK(card.settled_change);
        CHECK_EQUAL(0, shared.trial.settle);
        expect_record(UPDATE_TRIAL_NONE, 0);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(boot_retries_a_transient_storage_failure),
        TEST_CASE(rollback_retries_a_transient_storage_failure),
        TEST_CASE(promotion_retries_a_transient_storage_failure),
        TEST_CASE(promotion_can_resume_after_its_final_record_write_fails),
        TEST_CASE(failed_settle_keeps_work_pending),
        TEST_CASE(failed_cleanup_keeps_a_terminal_record),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
