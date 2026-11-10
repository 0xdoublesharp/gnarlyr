#pragma once

#include <stdint.h>

#include "library/library.h"
#include "playback/listening_log.h"

StoreLoad listening_track_read(const Library *library, uint16_t track, ListeningTrack *identity);
