#include "device/time_file.h"

#include "core/bytes.h"
#include "core/record.h"

enum {
    MAGIC_FIELD = 0,
    VERSION_FIELD = 4,
    OFFSET_FIELD = 6,
    UTC_FIELD = 8,
    RESERVED_FIELD = 12,
    CHECK_FIELD = 28
};

const char TIME_FILE_PATH[] = "/" FIRMWARE_DATA_DIRECTORY "/time.bin";

static const RecordKind KIND = {{'D', 'S', 'T', 'M'}, TIME_FILE_VERSION};

static bool framed(const uint8_t *bytes)
{
    for (int index = RESERVED_FIELD; index < CHECK_FIELD; index++) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return record_is(bytes, &KIND) && record_sealed(bytes, CHECK_FIELD);
}

bool time_file_parse(const uint8_t bytes[TIME_FILE_BYTES], HalWallTime *time)
{
    *time = (HalWallTime){.source = HAL_WALL_UNKNOWN};
    if (!framed(bytes)) {
        return false;
    }
    int16_t offset = (int16_t)bytes_le16(bytes + OFFSET_FIELD);
    uint32_t utc = bytes_le32(bytes + UTC_FIELD);
    if (offset < TIME_FILE_OFFSET_WEST || offset > TIME_FILE_OFFSET_EAST ||
        utc < TIME_FILE_EARLIEST_UTC || utc > TIME_FILE_LATEST_UTC) {
        return false;
    }
    *time = (HalWallTime){.utc_s = utc, .offset_minutes = offset, .source = HAL_WALL_COMPUTER};
    return true;
}
