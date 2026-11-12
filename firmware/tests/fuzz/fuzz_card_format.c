#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "device/card_format.h"
#include "device/fat.h"
#include "fuzz_source.h"

enum {
    CONTROL_BYTES = 16,
    STORED_SECTORS = 96,
};

typedef struct SparseCard {
    uint32_t sectors;
    uint32_t lba[STORED_SECTORS];
    uint8_t bytes[STORED_SECTORS][CARD_FORMAT_SECTOR_BYTES];
    uint32_t stored;
    uint32_t read_calls;
    uint32_t write_calls;
    uint32_t fail_read;
    uint32_t fail_write;
    uint32_t progress_calls;
    uint32_t cancel_progress;
    uint32_t last_done;
    uint32_t progress_total;
} SparseCard;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static int stored_index(const SparseCard *card, uint32_t lba)
{
    for (uint32_t index = 0; index < card->stored; index++) {
        if (card->lba[index] == lba) {
            return (int)index;
        }
    }
    return -1;
}

static bool all_zero(const uint8_t *bytes)
{
    for (uint32_t index = 0; index < CARD_FORMAT_SECTOR_BYTES; index++) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool store_sector(SparseCard *card, uint32_t lba, const uint8_t *bytes)
{
    int found = stored_index(card, lba);
    if (all_zero(bytes)) {
        if (found >= 0) {
            uint32_t last = --card->stored;
            card->lba[found] = card->lba[last];
            memcpy(card->bytes[found], card->bytes[last], CARD_FORMAT_SECTOR_BYTES);
        }
        return true;
    }
    uint32_t index;
    if (found >= 0) {
        index = (uint32_t)found;
    } else {
        if (card->stored == STORED_SECTORS) {
            return false;
        }
        index = card->stored++;
        card->lba[index] = lba;
    }
    memcpy(card->bytes[index], bytes, CARD_FORMAT_SECTOR_BYTES);
    return true;
}

static bool read_sectors(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    SparseCard *card = context;
    card->read_calls++;
    if (card->read_calls == card->fail_read || lba >= card->sectors ||
        count > card->sectors - lba) {
        return false;
    }
    uint8_t *out = buffer;
    for (uint32_t offset = 0; offset < count; offset++) {
        int found = stored_index(card, lba + offset);
        if (found < 0) {
            memset(out + (size_t)offset * CARD_FORMAT_SECTOR_BYTES, 0, CARD_FORMAT_SECTOR_BYTES);
        } else {
            memcpy(out + (size_t)offset * CARD_FORMAT_SECTOR_BYTES, card->bytes[found],
                   CARD_FORMAT_SECTOR_BYTES);
        }
    }
    return true;
}

static bool write_sectors(void *context, uint32_t lba, uint32_t count, const void *buffer)
{
    SparseCard *card = context;
    card->write_calls++;
    if (card->write_calls == card->fail_write || lba >= card->sectors ||
        count > card->sectors - lba || count > CARD_FORMAT_BUFFER_SECTORS) {
        return false;
    }
    const uint8_t *bytes = buffer;
    for (uint32_t offset = 0; offset < count; offset++) {
        if (!store_sector(card, lba + offset, bytes + (size_t)offset * CARD_FORMAT_SECTOR_BYTES)) {
            return false;
        }
    }
    return true;
}

static bool progress(void *context, uint32_t done, uint32_t total)
{
    SparseCard *card = context;
    card->progress_calls++;
    fuzz_require(total != 0 && done >= card->last_done && done <= total);
    fuzz_require(card->progress_total == 0 || card->progress_total == total);
    card->last_done = done;
    card->progress_total = total;
    return card->progress_calls != card->cancel_progress;
}

static void mutate_plan(CardFormatPlan *plan, uint8_t choice)
{
    switch (choice % 13) {
    case 0:
        return;
    case 1:
        plan->sectors ^= 1;
        break;
    case 2:
        plan->alignment ^= 1;
        break;
    case 3:
        plan->start ^= 1;
        break;
    case 4:
        plan->fat ^= 1;
        break;
    case 5:
        plan->fat_sectors ^= 1;
        break;
    case 6:
        plan->data ^= 1;
        break;
    case 7:
        plan->clusters ^= 1;
        break;
    case 8:
        plan->bitmap_clusters ^= 1;
        break;
    case 9:
        plan->root_cluster ^= 1;
        break;
    case 10:
        plan->cluster_shift ^= 1;
        break;
    case 11:
        plan->exfat = !plan->exfat;
        break;
    default:
        plan->sectors = 0;
        break;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint32_t SECTOR_COUNTS[] = {
        131072,
        262144,
        4194304,
        8388608,
        CARD_FORMAT_SDHC_SECTORS,
        CARD_FORMAT_SDHC_SECTORS + 1,
        128u * 1024 * 1024,
        256u * 2 * 1024 * 1024,
    };
    static const uint32_t ALLOCATION_UNITS[] = {
        0, 512, 8192, 24576, 32768, 49152, 65536, 131072,
    };
    static const char *const PATHS[] = {
        "/fuzz.bin",
        "/long fuzz name.bin",
        "/bad:name.bin",
        "/folder/file.bin",
    };
    static const uint32_t FILE_SIZES[] = {0, 1, 511, 512, 4096, 65536};
    uint8_t controls[CONTROL_BYTES] = {0};
    size_t copied = size < sizeof controls ? size : sizeof controls;
    memcpy(controls, data, copied);

    CardFormatPlan raw;
    card_format_plan(&raw, get32(controls), get32(controls + 4));

    uint32_t sectors =
        SECTOR_COUNTS[controls[0] % (sizeof SECTOR_COUNTS / sizeof SECTOR_COUNTS[0])];
    uint32_t allocation =
        ALLOCATION_UNITS[controls[1] % (sizeof ALLOCATION_UNITS / sizeof ALLOCATION_UNITS[0])];
    CardFormatPlan plan;
    if (!card_format_plan(&plan, sectors, allocation)) {
        return 0;
    }
    fuzz_require(plan.start < plan.data && plan.data < plan.sectors);
    fuzz_require(plan.start % plan.alignment == 0 && plan.data % plan.alignment == 0);

    SparseCard card = {.sectors = sectors,
                       .fail_write = controls[3] == 0 ? 0 : 1u + controls[3],
                       .cancel_progress = controls[4] == 0 ? 0 : 1u + controls[4]};
    CardFormat format = {.device = {read_sectors, &card, write_sectors},
                         .plan = plan,
                         .progress = progress,
                         .context = &card};
    mutate_plan(&format.plan, controls[2]);
    if ((controls[5] & 1u) != 0) {
        format.device.write = NULL;
    }
    bool made = card_format_run(&format);
    if (made) {
        fuzz_require(format.done == format.total && card.last_done == format.total);
    }
    if (!made || controls[2] % 13 != 0 || format.device.write == NULL) {
        return 0;
    }

    card.read_calls = 0;
    card.write_calls = 0;
    card.fail_read = controls[6] == 0 ? 0 : 1u + (controls[6] & 31u);
    card.fail_write = controls[7] == 0 ? 0 : 1u + (controls[7] & 31u);
    FatVolume volume;
    if (!fat_mount(&volume, format.device)) {
        return 0;
    }
    fuzz_require(volume.type == (plan.exfat ? FAT_TYPE_EXFAT : FAT_TYPE_32));
    const char *path = PATHS[controls[8] % (sizeof PATHS / sizeof PATHS[0])];
    uint32_t file_size = FILE_SIZES[controls[9] % (sizeof FILE_SIZES / sizeof FILE_SIZES[0])];
    FatReserveResult result = fat_reserve(&volume, path, file_size);
    fuzz_require(result >= FAT_RESERVE_EXISTS && result <= FAT_RESERVE_FAILED);
    return 0;
}
