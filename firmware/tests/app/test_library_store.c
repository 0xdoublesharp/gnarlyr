#include <string.h>

#include "check.h"
#include "favorite_playlist.h"
#include "fixture_library.h"
#include "library_store.h"

enum {
    FILE_SLOTS = 2
};

static bool handles[FILE_SLOTS];
static const uint8_t *contents;
static uint32_t length;

bool hal_file_open(HalFile *file, const char *path)
{
    *file = (HalFile){.handle = -1};
    if (strcmp(path, LIBRARY_STORE_PATH) != 0 || contents == NULL) {
        return false;
    }
    for (int slot = 0; slot < FILE_SLOTS; slot++) {
        if (!handles[slot]) {
            handles[slot] = true;
            *file = (HalFile){.handle = slot, .size = length};
            return true;
        }
    }
    return false;
}

bool hal_file_read(HalFile file, uint32_t offset, void *buffer, uint32_t count)
{
    if (file.handle < 0 || file.handle >= FILE_SLOTS || !handles[file.handle] || offset > length ||
        count > length - offset) {
        return false;
    }
    memcpy(buffer, contents + offset, count);
    return true;
}

void hal_file_close(HalFile *file)
{
    handles[file->handle] = false;
    file->handle = -1;
}

void favorite_playlist_attach(Library *library)
{
    (void)library;
}

static void repeated_open_reuses_one_file_slot(void)
{
    LibraryStore store = {0};
    contents = fixture_bytes(&length);
    HalFile other;
    CHECK(hal_file_open(&other, LIBRARY_STORE_PATH));
    for (int repeat = 0; repeat < 20; repeat++) {
        CHECK(library_store_open(&store));
        CHECK_EQUAL(FIXTURE_TRACKS, library_music_count(&store.library, LIBRARY_TRACKS));
        CHECK(handles[other.handle]);
    }
    contents = NULL;
    CHECK(!library_store_open(&store));
    CHECK(handles[other.handle]);
    CHECK(!handles[1]);
    hal_file_close(&other);
}

int main(void)
{
    static const TestCase cases[] = {TEST_CASE(repeated_open_reuses_one_file_slot)};
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
