#pragma once

#if defined(__GNUC__) && !defined(__clang__)
#define CORE_SEAM __attribute__((noipa, externally_visible))
#else
#define CORE_SEAM
#endif
