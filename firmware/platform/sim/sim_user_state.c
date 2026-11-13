#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hal/system.h"
#include "settings/user_state.h"
#include "sim_storage.h"

enum {
    PATH_CAPACITY = 1024,
    BUFFER_BYTES = 16384
};

static bool host_path(const UserStatePlace *place, char *resolved)
{
    return sim_storage_host_path(place->path, resolved, PATH_CAPACITY);
}

static uint32_t file_size(void *context, const UserStatePlace *place)
{
    (void)context;
    char resolved[PATH_CAPACITY];
    struct stat found;
    if (!host_path(place, resolved) || stat(resolved, &found) != 0 || !S_ISREG(found.st_mode)) {
        return USER_STATE_MISSING;
    }
    return (uint32_t)found.st_size;
}

static bool transfer(const UserStatePlace *place, UserStateChunk chunk, bool writing)
{
    char resolved[PATH_CAPACITY];
    FILE *handle = host_path(place, resolved) ? fopen(resolved, writing ? "r+b" : "rb") : NULL;
    if (handle == NULL) {
        return false;
    }
    bool moved = fseek(handle, (long)chunk.offset, SEEK_SET) == 0 &&
                 (writing ? fwrite(chunk.bytes, 1, chunk.length, handle)
                          : fread(chunk.bytes, 1, chunk.length, handle)) == chunk.length;
    return fclose(handle) == 0 && moved;
}

static bool file_read(void *context, const UserStatePlace *place, UserStateChunk chunk)
{
    (void)context;
    return transfer(place, chunk, false);
}

static bool file_write(void *context, const UserStatePlace *place, UserStateChunk chunk)
{
    (void)context;
    return transfer(place, chunk, true);
}

static void make_parents(char *resolved)
{
    for (char *at = resolved + 1; *at != '\0'; at++) {
        if (*at == '/') {
            *at = '\0';
            mkdir(resolved, 0755);
            *at = '/';
        }
    }
}

static bool file_create(void *context, const UserStatePlace *place, uint32_t size)
{
    (void)context;
    char resolved[PATH_CAPACITY];
    if (!host_path(place, resolved)) {
        return false;
    }
    make_parents(resolved);
    FILE *handle = fopen(resolved, "wb");
    if (handle == NULL) {
        return false;
    }
    bool sized = ftruncate(fileno(handle), (off_t)size) == 0;
    return fclose(handle) == 0 && sized;
}

static bool file_remove(void *context, const UserStatePlace *place)
{
    (void)context;
    char resolved[PATH_CAPACITY];
    return host_path(place, resolved) && remove(resolved) == 0;
}

bool hal_system_settle_user_state(bool requested)
{
    static uint8_t buffer[BUFFER_BYTES];
    if (!requested) {
        return true;
    }
    UserStateIo io = {file_size,   file_read, file_create, file_write,
                      file_remove, NULL,      buffer,      sizeof buffer};
    return user_state_run(&io, hal_system_version(), false);
}
