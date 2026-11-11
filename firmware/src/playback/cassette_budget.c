#include "playback/cassette.h"

bool cassette_budget(uint8_t minutes, uint32_t used_ms, uint32_t added_ms)
{
    uint32_t budget = (uint32_t)minutes * 60000;
    return (minutes == 30 || minutes == 45) && added_ms > 0 && used_ms <= budget &&
           added_ms <= budget - used_ms;
}
