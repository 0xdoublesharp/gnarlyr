#include "playback/mix_identity.h"

#include "core/hash.h"

enum {
    START_FIELD = 2,
    END_FIELD = 6
};

static bool dots(const char *part, uint16_t length)
{
    return (length == 1 || length == 2) && part[0] == '.' && part[length - 1] == '.';
}

static bool byte_valid(uint8_t byte)
{
    return byte >= 32 && byte != 127 && byte != ':' && byte != '\\';
}

static uint16_t path_length(const char *path)
{
    uint16_t start = 0;
    for (uint16_t index = 0; index < MIX_PATH_BYTES; index++) {
        uint8_t byte = (uint8_t)path[index];
        if (byte == 0 || byte == '/') {
            uint16_t length = (uint16_t)(index - start);
            if (length == 0 || dots(path + start, length)) {
                return 0;
            }
            if (byte == 0) {
                return index;
            }
            start = (uint16_t)(index + 1);
        } else if (!byte_valid(byte)) {
            return 0;
        }
    }
    return 0;
}

static bool range_valid(const MixIdentity *identity)
{
    return identity->end_ms == 0 || identity->end_ms > identity->start_ms;
}

uint16_t mix_identity_size(const MixIdentity *identity)
{
    uint16_t length = path_length(identity->path);
    return length == 0 || !range_valid(identity) ? 0 : (uint16_t)(MIX_RECORD_FIELDS + length);
}

static uint16_t encode_fields(const MixIdentity *identity, uint8_t *bytes)
{
    uint16_t size = mix_identity_size(identity);
    if (size != 0) {
        store_put16(bytes, (uint16_t)(size - MIX_RECORD_FIELDS));
        store_put32(bytes + START_FIELD, identity->start_ms);
        store_put32(bytes + END_FIELD, identity->end_ms);
    }
    return size;
}

uint16_t mix_identity_encode(const MixIdentity *identity, uint8_t *bytes)
{
    uint16_t size = encode_fields(identity, bytes);
    for (uint16_t index = MIX_RECORD_FIELDS; index < size; index++) {
        bytes[index] = (uint8_t)identity->path[index - MIX_RECORD_FIELDS];
    }
    return size;
}

uint16_t mix_identity_decode(const uint8_t *bytes, uint32_t available, MixIdentity *identity)
{
    *identity = (MixIdentity){0};
    if (available < MIX_RECORD_MIN) {
        return 0;
    }
    uint16_t length = store_get16(bytes);
    if (length == 0 || length >= MIX_PATH_BYTES || length > available - MIX_RECORD_FIELDS) {
        return 0;
    }
    identity->start_ms = store_get32(bytes + START_FIELD);
    identity->end_ms = store_get32(bytes + END_FIELD);
    for (uint16_t index = 0; index < length; index++) {
        identity->path[index] = (char)bytes[MIX_RECORD_FIELDS + index];
    }
    uint16_t size = mix_identity_size(identity);
    return size == MIX_RECORD_FIELDS + length ? size : 0;
}

static bool terminated(const char *path, uint32_t size)
{
    for (uint32_t index = 0; index < size; index++) {
        if (path[index] == 0) {
            return true;
        }
    }
    return false;
}

static StoreLoad read_path(const Library *library, uint32_t offset, char *path)
{
    LibrarySectionInfo strings = library->sections[LIBRARY_STRINGS];
    if (offset >= strings.count) {
        return STORE_CORRUPT;
    }
    uint32_t size = strings.count - offset;
    size = size < MIX_PATH_BYTES ? size : MIX_PATH_BYTES;
    if (!library->source.read(library->source.context, strings.offset + offset, path, size)) {
        return STORE_UNREADABLE;
    }
    return terminated(path, size) ? STORE_READY : STORE_CORRUPT;
}

StoreLoad mix_identity_read(const Library *library, uint16_t track, MixIdentity *identity)
{
    LibraryTrack record;
    *identity = (MixIdentity){0};
    if (track >= library_count(library, LIBRARY_TRACKS)) {
        return STORE_CORRUPT;
    }
    if (!library_read_track(library, track, &record)) {
        return STORE_UNREADABLE;
    }
    StoreLoad load = read_path(library, record.path, identity->path);
    identity->start_ms = record.start_ms;
    identity->end_ms = record.end_ms;
    return load == STORE_READY && mix_identity_size(identity) == 0 ? STORE_CORRUPT : load;
}

bool mix_identity_equal(const MixIdentity *first, const MixIdentity *second)
{
    if (first->start_ms != second->start_ms || first->end_ms != second->end_ms) {
        return false;
    }
    for (int index = 0; index < MIX_PATH_BYTES; index++) {
        if (first->path[index] != second->path[index]) {
            return false;
        }
        if (first->path[index] == 0) {
            return true;
        }
    }
    return false;
}

uint32_t mix_identity_hash(const MixIdentity *identity)
{
    uint8_t fields[MIX_RECORD_FIELDS];
    uint16_t size = encode_fields(identity, fields);
    if (size == 0) {
        return 0;
    }
    uint32_t hash = hash_update(HASH_START, fields, sizeof fields);
    return hash_update(hash, identity->path, (uint16_t)(size - MIX_RECORD_FIELDS));
}
