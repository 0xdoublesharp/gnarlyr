# Original theme sources

`pocket-scope` is an original dark green instrument-panel design that evokes desktop audio players. `desktop-98` uses warm grey surfaces, blue accents, and beveled pixel details to evoke late-1990s desktops. `vice-86` uses an indigo twilight palette, striped sunsets, palm silhouettes, and a pixel cassette icon to evoke the neon coast of the mid-1980s. All three packs use original PNG artwork and manifests offered under CC0-1.0.

Build any example folder with `python3 firmware/tools/theme_pack.py FOLDER --output DIRECTORY`. If the output directory exists, the builder names the pack after its UUID. Keep generated `.gtheme` files on Scratch or your player card, outside Git. The [format guide](../../docs/theme-format.md) lists all palette roles and image slots.

The simulator capture runner is `firmware/tools/theme_pack_captures.py`. Build `echo_sim` and `theme_pack_tool` through `tools/firmware.py`, then supply `--sim`, `--state-tool`, and a Scratch `--output` directory. It builds all the examples and captures Home, Music, Now Playing, Settings, and the Theme picker in both orientations at three text sizes. It also creates original test audio, a contact sheet, and simulator frame-time samples.
