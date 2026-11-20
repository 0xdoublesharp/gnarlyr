#pragma once

#include <stdbool.h>

#include "browse_screen.h"

bool artist_album_only(const BrowseScreen *browse, BrowseQuery albums, BrowseQuery *album);
