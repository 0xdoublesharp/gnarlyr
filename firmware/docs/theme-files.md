# Theme palette files

The five built-in themes can be overridden by bounded palette files on internal storage: `/.freshbase/themes/graphite.bin`, `neon.bin`, `sunset.bin`, `glass.bin` and `classic.bin`. The filenames identify slots; the screen labels come from the language pack. A missing or invalid file uses that slot's built-in palette. Reconnecting USB invalidates the cached palette so an edited file is loaded again.

Each file is exactly 52 bytes. Bytes 0–3 are `DSTH`; byte 4 is version 1; byte 5 holds horizon and header-rule flags; byte 6 is the color count (20) and byte 7 is zero. Bytes 8–47 are the 20 little-endian RGB565 colors in `ThemeColors` field order. Bytes 48–51 hold FNV-1a of bytes 0–47, seeded with 2166136261. `theme_palette_encode` and `theme_palette_decode` are the shared format implementation.

Palettes change colors and decoration flags. Text sizes, spacing and navigation stay consistent. The Graphite slot also applies the selected highlight color.
