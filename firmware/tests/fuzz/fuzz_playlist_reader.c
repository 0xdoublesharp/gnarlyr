#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fuzz_source.h"
#include "library/playlist_reader.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_entry(const char *entry, uint32_t capacity)
{
    fuzz_require(memchr(entry, '\0', capacity) != NULL);
    fuzz_require(entry[0] != '\0');
    fuzz_require(entry[0] != '#');
}

static void compare_next(PlaylistReader *first, PlaylistReader *second, uint32_t capacity,
                         bool *finished)
{
    char first_entry[PLAYLIST_ENTRY_CAPACITY];
    char second_entry[PLAYLIST_ENTRY_CAPACITY];
    memset(first_entry, 0xA5, sizeof first_entry);
    memset(second_entry, 0x5A, sizeof second_entry);
    uint32_t before = first->offset;
    bool first_read = playlist_reader_next(first, first_entry, capacity);
    bool second_read = playlist_reader_next(second, second_entry, capacity);
    fuzz_require(first_read == second_read);
    fuzz_require(first->offset == second->offset);
    fuzz_require(first->start_ms == second->start_ms);
    fuzz_require(first->offset <= first->size);
    if (first_read) {
        fuzz_require(first->offset > before);
        check_entry(first_entry, capacity);
        check_entry(second_entry, capacity);
        fuzz_require(strcmp(first_entry, second_entry) == 0);
    }
    *finished = !first_read;
}

static void check_playlist(const uint8_t *data, uint32_t size, uint32_t capacity)
{
    FuzzSource first_source = {.bytes = data, .size = size};
    FuzzSource second_source = first_source;
    PlaylistReader first;
    PlaylistReader second;
    fuzz_require(playlist_reader_open(&first, fuzz_source(&first_source), size));
    fuzz_require(playlist_reader_open(&second, fuzz_source(&second_source), size));
    bool finished = false;
    for (uint32_t call = 0; call <= size && !finished; call++) {
        compare_next(&first, &second, capacity, &finished);
    }
    fuzz_require(finished);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2 || size - 2 > UINT32_MAX) {
        return 0;
    }
    uint32_t selected = (uint32_t)data[0] | (uint32_t)data[1] << 8;
    uint32_t capacity = 1u + selected % PLAYLIST_ENTRY_CAPACITY;
    check_playlist(data + 2, (uint32_t)(size - 2), capacity);
    return 0;
}
