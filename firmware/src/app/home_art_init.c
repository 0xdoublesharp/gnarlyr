#include "home_art.h"

void home_art_init(HomeArt *art, CoverCache *covers, const LibraryStore *store, Player *player,
                   const Theme *theme)
{
    *art = (HomeArt){.covers = covers, .store = store, .player = player, .theme = theme};
}
