#include "mix_private.h"

#include "core/hash.h"

static uint16_t path_length(const char *path)
{
    uint16_t component = 0;
    uint16_t start = 0;
    for (uint16_t index = 0; index < MIX_PATH_BYTES; index++) {
        uint8_t byte = (uint8_t)path[index];
        if (byte == 0 || byte == '/') {
            if (component == 0 || (component == 1 && path[start] == '.') ||
                (component == 2 && path[start] == '.' && path[start + 1] == '.')) {
                return 0;
            }
            if (byte == 0) {
                return index;
            }
            component = 0;
            start = (uint16_t)(index + 1);
        } else {
            if (byte < 32 || byte == 127 || byte == ':' || byte == '\\') {
                return 0;
            }
            component++;
        }
    }
    return 0;
}

bool mix_identity_valid(const MixIdentity *identity)
{
    return path_length(identity->path) != 0 &&
           (identity->end_ms == 0 || identity->end_ms > identity->start_ms);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (int byte = 0; byte < 4; byte++) {
        p[byte] = (uint8_t)(value >> (8 * byte));
    }
}

uint16_t mix_identity_encode(const MixIdentity *identity, uint8_t *bytes)
{
    uint16_t length = path_length(identity->path);
    if (length == 0 || (identity->end_ms != 0 && identity->end_ms <= identity->start_ms)) {
        return 0;
    }
    bytes[0] = (uint8_t)length;
    bytes[1] = (uint8_t)(length >> 8);
    put32(bytes + 2, identity->start_ms);
    put32(bytes + 6, identity->end_ms);
    for (uint16_t index = 0; index < length; index++) {
        bytes[10 + index] = (uint8_t)identity->path[index];
    }
    return (uint16_t)(10 + length);
}

MixLoad mix_identity_read(const Library *library, uint16_t track, MixIdentity *identity)
{
    *identity = (MixIdentity){0};
    if (track >= library_count(library, LIBRARY_TRACKS)) {
        return MIX_CORRUPT;
    }
    LibraryTrack record;
    if (!library_read_track(library, track, &record)) {
        return MIX_UNREADABLE;
    }
    LibrarySectionInfo strings = library->sections[LIBRARY_STRINGS];
    if (record.path >= strings.count) {
        return MIX_CORRUPT;
    }
    uint32_t size = strings.count - record.path;
    size = size < MIX_PATH_BYTES ? size : MIX_PATH_BYTES;
    if (!library->source.read(library->source.context, strings.offset + record.path, identity->path,
                              size)) {
        return MIX_UNREADABLE;
    }
    identity->start_ms = record.start_ms;
    identity->end_ms = record.end_ms;
    for (uint32_t index = 0; index < size; index++) {
        if (identity->path[index] == 0) {
            return mix_identity_valid(identity) ? MIX_READY : MIX_CORRUPT;
        }
    }
    return MIX_CORRUPT;
}

bool mix_identity_equal(const MixIdentity *a, const MixIdentity *b)
{
    if (a->start_ms != b->start_ms || a->end_ms != b->end_ms) {
        return false;
    }
    for (int index = 0; index < MIX_PATH_BYTES; index++) {
        if (a->path[index] != b->path[index]) {
            return false;
        }
        if (a->path[index] == 0) {
            return true;
        }
    }
    return false;
}

uint32_t mix_identity_hash(const MixIdentity *identity)
{
    uint8_t bytes[MIX_RECORD_MAX];
    uint16_t size = mix_identity_encode(identity, bytes);
    return size == 0 ? 0 : hash_update(HASH_START, bytes, size);
}
