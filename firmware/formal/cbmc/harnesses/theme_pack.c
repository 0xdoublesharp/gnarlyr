#include "theme_pack.h"
#include "formal.h"

void harness(void)
{
    ThemePack pack;
    formal_card_fill();
    if (!theme_pack_open(&pack, formal_card_source(), formal_card_size)) {
        return;
    }
    __CPROVER_assert(pack.asset_count <= THEME_PACK_MAX_ASSETS,
                     "an accepted pack names few assets");
    for (unsigned at = 0; at < pack.asset_count; at++) {
        __CPROVER_assert(pack.assets[at].offset <= formal_card_size &&
                             pack.assets[at].bytes <= formal_card_size - pack.assets[at].offset,
                         "every accepted asset lies inside the file");
    }
    __CPROVER_assert(pack.name[THEME_PACK_NAME_BYTES - 1] == '\0',
                     "the name ends inside its field");
    __CPROVER_assert(pack.version[31] == '\0', "the version ends inside its field");
}
