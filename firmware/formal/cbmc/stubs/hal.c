#include "formal.h"
#include "hal/language.h"
#include "hal/storage.h"
#include "hal/system.h"

#ifndef FORMAL_VERSION_BYTES
#define FORMAL_VERSION_BYTES 24
#endif

#ifndef FORMAL_LANGUAGE_MEMORY
#define FORMAL_LANGUAGE_MEMORY 64
#endif

static char version_text[FORMAL_VERSION_BYTES];
static uint8_t language_memory[FORMAL_LANGUAGE_MEMORY];

void formal_version_fill(void);

void formal_version_fill(void)
{
    __CPROVER_havoc_object(version_text);
    version_text[formal_below(FORMAL_VERSION_BYTES)] = '\0';
}

const char *hal_system_version(void)
{
    return version_text;
}

bool hal_system_restart_pending(void)
{
    return nondet_bool();
}

HalVolume hal_file_volume(void)
{
    return (HalVolume)formal_below(HAL_VOLUME_INTERNAL + 1);
}

bool hal_file_open(HalFile *file, const char *path)
{
    __CPROVER_assert(__CPROVER_r_ok(path, 1), "an opened path is readable");
    if (nondet_bool()) {
        return false;
    }
    *file = (HalFile){.handle = 1, .size = formal_card_size};
    return true;
}

bool hal_file_read(HalFile file, uint32_t offset, void *buffer, uint32_t length)
{
    __CPROVER_assert(file.handle == 1, "only an opened file is read");
    return formal_card_read(NULL, offset, buffer, length);
}

void hal_file_close(HalFile *file)
{
    file->handle = 0;
}

bool hal_internal_file_read(const char *path, void *buffer, uint32_t length)
{
    __CPROVER_assert(__CPROVER_r_ok(path, 1), "a read path is readable");
    __CPROVER_assert(__CPROVER_w_ok(buffer, length), "an internal read fits its buffer");
    __CPROVER_havoc_slice(buffer, length);
    return nondet_bool();
}

bool hal_internal_file_write_at(const char *path, uint32_t offset, const void *bytes,
                                uint32_t length)
{
    (void)offset;
    __CPROVER_assert(__CPROVER_r_ok(path, 1), "a written path is readable");
    __CPROVER_assert(__CPROVER_r_ok(bytes, length), "a written span is readable");
    return nondet_bool();
}

bool hal_file_write_at(const char *path, uint32_t offset, const void *bytes, uint32_t length)
{
    return hal_internal_file_write_at(path, offset, bytes, length);
}

uint8_t *hal_language_memory(uint32_t *capacity)
{
    *capacity = FORMAL_LANGUAGE_MEMORY;
    return language_memory;
}

bool hal_language_read(HalLanguagePack pack, uint32_t offset, void *buffer, uint32_t length)
{
    __CPROVER_assert(pack.copy < HAL_LANGUAGE_COPIES, "a built-in pack copy exists");
    return formal_card_read(NULL, offset, buffer, length);
}

void hal_audio_service(void)
{
}
