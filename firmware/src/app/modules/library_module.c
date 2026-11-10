#include "modules/modules.h"
#include "music_walk.h"

static void bind(AppScreens *screens)
{
    (void)screens;
}

const LibraryModuleEntry LIBRARY_MODULE_ENTRY = {
    .module = {SCREEN_MODULE_MAGIC(SCREEN_MODULE_LIBRARY), bind},
    .music_fingerprint = music_walk_fingerprint,
};
