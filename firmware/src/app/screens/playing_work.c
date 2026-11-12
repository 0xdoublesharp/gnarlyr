#include "playing_work.h"

PlayingModuleWork playing_module_work;

bool playing_work_claim_shuffle(void)
{
    bool fresh = playing_module_work.owner != PLAYING_WORK_SHUFFLE;
    playing_module_work.owner = PLAYING_WORK_SHUFFLE;
    return fresh;
}

PlayingStyleWork *playing_work_styles(void)
{
    PlayingStyleWork *styles = &playing_module_work.as.styles;
    if (playing_module_work.owner != PLAYING_WORK_STYLES) {
        playing_module_work.owner = PLAYING_WORK_STYLES;
        styles->meter = (MeterState){.frame = UINT32_MAX};
    }
    return styles;
}
