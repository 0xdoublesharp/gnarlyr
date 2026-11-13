#include "update.h"

#include <stddef.h>

#include "board.h"
#include "core/seam.h"
#include "device/fat.h"
#include "device/loader_request.h"
#include "emmc.h"
#include "internal_volume.h"

typedef struct RequestFile {
    FatVolume *volume;
    FatFile file;
} RequestFile;

static bool request_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    RequestFile *request = context;
    return fat_read(request->volume, &request->file, offset, buffer, length);
}

static bool request_write(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    RequestFile *request = context;
    return fat_write(request->volume, &request->file, offset, bytes, length);
}

static bool checked;

static bool open_request(RequestFile *request)
{
    EmmcInfo info;
    if (!emmc_start(&info)) {
        return false;
    }
    request->volume = internal_volume_remount();
    if (request->volume == NULL) {
        return false;
    }
    checked = true;
    return fat_open(request->volume, LOADER_REQUEST_PATH, &request->file);
}

CORE_SEAM void update_check_loader_request(void)
{
    RequestFile open;
    if (checked || !open_request(&open)) {
        return;
    }
    LoaderRequestFile request = {request_read, request_write, open.file.size, &open};
    if (loader_request_consume(&request)) {
        board_loader_reboot();
    }
}
