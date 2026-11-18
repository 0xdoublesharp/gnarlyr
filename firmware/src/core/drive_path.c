#include "core/drive_path.h"

#include <stddef.h>

const char *drive_path_internal(const char *path)
{
    if (path[0] != '/' || path[1] != DRIVE_PATH_INTERNAL || (path[2] != '\0' && path[2] != '/')) {
        return NULL;
    }
    return path[2] == '/' ? path + 2 : "/";
}
