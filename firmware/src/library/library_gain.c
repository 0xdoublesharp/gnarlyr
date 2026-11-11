#include "library/library_gain.h"

#include "core/hash.h"
#include "library_gain_format.h"

static uint32_t extension_offset(const Library *library)
{
    uint32_t end = LIBRARY_HEADER_SIZE;
    for (uint32_t section = 0; section < LIBRARY_SECTION_COUNT; section++) {
        LibrarySectionInfo info = library->sections[section];
        uint32_t next = info.offset + info.count * info.record_size;
        end = next > end ? next : end;
    }
    return end;
}

static bool read_bytes(const Library *library, uint32_t offset, uint8_t *out, uint32_t size)
{
    return offset <= library->size && size <= library->size - offset &&
           library->source.read(library->source.context, offset, out, size);
}

static bool valid_header(const Library *library, const uint8_t *header, uint32_t offset)
{
    uint32_t count = library_count(library, LIBRARY_TRACKS);
    uint64_t length = LIBRARY_GAIN_HEADER_SIZE + (uint64_t)count * LIBRARY_GAIN_RECORD_SIZE;
    return header[4] == LIBRARY_GAIN_VERSION && header[5] == 0 &&
           header[6] == LIBRARY_GAIN_RECORD_SIZE && header[7] == 0 &&
           hash_load(header + 8) == count &&
           hash_load(header + 12) == library_fingerprint(library) &&
           hash_load(header + 16) == hash_update(HASH_START, header, 16) &&
           length <= library->size - offset;
}

static bool read_gain_record(const Library *library, uint32_t offset, uint32_t index,
                             ReplayGain *gain)
{
    uint8_t record[LIBRARY_GAIN_RECORD_SIZE];
    offset += LIBRARY_GAIN_HEADER_SIZE + index * LIBRARY_GAIN_RECORD_SIZE;
    if (!read_bytes(library, offset, record, sizeof record) ||
        hash_load(record + 16) !=
            library_gain_record_hash(library_fingerprint(library), index, record)) {
        return false;
    }
    ReplayGain decoded = {hash_load(record), hash_load(record + 4), hash_load(record + 8),
                          hash_load(record + 12)};
    if (decoded.track_safe > decoded.track || decoded.album_safe > decoded.album ||
        (decoded.track != 0 && decoded.track_safe == 0) ||
        (decoded.album != 0 && decoded.album_safe == 0)) {
        return false;
    }
    *gain = decoded;
    return true;
}

bool library_read_replay_gain(const Library *library, uint32_t index, ReplayGain *gain)
{
    *gain = (ReplayGain){0};
    if (!library->content_known || index >= library_count(library, LIBRARY_TRACKS)) {
        return false;
    }
    uint32_t offset = extension_offset(library);
    if (offset > library->size || library->size - offset < 4) {
        return true;
    }
    uint8_t header[LIBRARY_GAIN_HEADER_SIZE];
    if (!read_bytes(library, offset, header, 4)) {
        return false;
    }
    if (hash_load(header) != LIBRARY_GAIN_MAGIC) {
        return true;
    }
    return read_bytes(library, offset, header, sizeof header) &&
           valid_header(library, header, offset) && read_gain_record(library, offset, index, gain);
}

uint32_t library_replay_gain_for(const Library *library, uint32_t index, ReplayGainMode mode,
                                 bool prevent_clipping)
{
    ReplayGain gain;
    if (mode <= REPLAY_GAIN_OFF || mode >= REPLAY_GAIN_MODE_COUNT ||
        !library_read_replay_gain(library, index, &gain)) {
        return REPLAY_GAIN_UNITY;
    }
    uint32_t track = prevent_clipping ? gain.track_safe : gain.track;
    uint32_t album = prevent_clipping ? gain.album_safe : gain.album;
    uint32_t preferred = mode == REPLAY_GAIN_ALBUM ? album : track;
    uint32_t fallback = mode == REPLAY_GAIN_ALBUM ? track : album;
    return preferred != 0 ? preferred : fallback != 0 ? fallback : REPLAY_GAIN_UNITY;
}
