#include "core/format_chain.h"

#include <stddef.h>

FormatAge format_age(uint16_t current, uint32_t version)
{
    if (version == 0) {
        return FORMAT_AGE_INVALID;
    }
    if (version < current) {
        return FORMAT_AGE_OLDER;
    }
    return version == current ? FORMAT_AGE_CURRENT : FORMAT_AGE_NEWER;
}

bool format_upgrade(const FormatChain *chain, uint32_t version, FormatBytes *file)
{
    FormatAge age = format_age(chain->current, version);
    if (age == FORMAT_AGE_INVALID || age == FORMAT_AGE_NEWER) {
        return false;
    }
    for (uint32_t step = version; step < chain->current; step++) {
        if (chain->steps == NULL || chain->steps[step - 1] == NULL ||
            !chain->steps[step - 1](file)) {
            return false;
        }
    }
    return true;
}
