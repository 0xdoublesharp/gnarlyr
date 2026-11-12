#pragma once

#include "theme_signature.h"

#define THEME_TRUSTED_KEY_COUNT 0

_Static_assert(THEME_TRUSTED_KEY_COUNT <= 4, "");

#if THEME_TRUSTED_KEY_COUNT > 0
static const ThemeTrustedKey THEME_TRUSTED_KEYS[THEME_TRUSTED_KEY_COUNT] = {{{0}, {0}}};
#endif
