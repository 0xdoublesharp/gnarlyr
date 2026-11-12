#include <stdio.h>
#include <string.h>

#include "fake_hal.h"
#include "settings/settings.h"
#include "theme_runtime.h"
#include "theme_signature.h"

static uint8_t bytes[THEME_PACK_MAX_BYTES];
static uint32_t length;

static bool read_bytes(void *context, uint32_t offset, void *buffer, uint32_t count)
{
    (void)context;
    if (offset > length || count > length - offset) {
        return false;
    }
    memcpy(buffer, bytes + offset, count);
    return true;
}

static bool load(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    size_t size = fread(bytes, 1, sizeof bytes, file);
    bool valid = !ferror(file) && fgetc(file) == EOF;
    fclose(file);
    length = (uint32_t)size;
    return valid;
}

static bool write_file(const char *directory, const char *name, const uint8_t *data, uint32_t size)
{
    char path[1024];
    int count = snprintf(path, sizeof path, "%s/%s", directory, name);
    if (count < 0 || (unsigned)count >= sizeof path) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    bool done = fwrite(data, 1, size, file) == size;
    return fclose(file) == 0 && done;
}

int main(int argc, char **argv)
{
    if (argc < 3 || !load(argv[2])) {
        return 2;
    }
    ThemePack pack;
    if (!theme_pack_open(&pack, (ByteSource){read_bytes, NULL}, length)) {
        return 1;
    }
    if (strcmp(argv[1], "--check") == 0) {
        if (argc == 4) {
            FILE *file = fopen(argv[3], "rb");
            uint8_t key[32];
            if (file == NULL) {
                return 2;
            }
            bool complete = fread(key, 1, sizeof key, file) == sizeof key && fgetc(file) == EOF;
            fclose(file);
            if (!complete || length < THEME_PACK_SIGNATURE_BYTES ||
                !theme_signature_verify((ByteSource){read_bytes, NULL},
                                        length - THEME_PACK_SIGNATURE_BYTES, bytes + length - 64,
                                        key)) {
                return 1;
            }
        }
        printf("%s: %u assets\n", pack.name, pack.asset_count);
        return 0;
    }
    if (argc != 6 || strcmp(argv[1], "--state") != 0 || argv[4][0] < '0' || argv[4][0] > '2' ||
        argv[5][0] < '0' || argv[5][0] > '2') {
        return 2;
    }
    fake_hal_reset();
    static const char path[] = "/" FIRMWARE_DATA_DIRECTORY "/themes/candidate.gtheme";
    fake_hal_add_file(path, bytes, length);
    ThemeRuntime runtime = {0};
    theme_runtime_boot(&runtime);
    if (!theme_runtime_load(&runtime, "candidate.gtheme") || !theme_runtime_save(&runtime, -1)) {
        return 1;
    }
    uint32_t size;
    const uint8_t *selection = fake_hal_internal_file_bytes(THEME_SELECTION_PATH, &size);
    Settings settings = settings_defaults();
    settings.text_size = (TextSize)(argv[4][0] - '0');
    settings.orientation = (Orientation)(argv[5][0] - '0');
    uint8_t encoded[SETTINGS_FILE_SIZE];
    settings_encode(&settings, encoded);
    return write_file(argv[3], "theme.bin", selection, size) &&
                   write_file(argv[3], "settings.bin", encoded, sizeof encoded)
               ? 0
               : 1;
}
