# Original theme sources

`pocket-scope` is an original dark green instrument-panel design that evokes desktop audio players. `desktop-98` uses warm grey surfaces, blue accents and original beveled pixel details to evoke late-1990s desktops. Neither contains copied logos, trademarks, interface screenshots or third-party artwork. The PNG artwork and manifests in both folders are offered under CC0-1.0.

Build either folder with `python3 tools/theme_pack.py FOLDER --output DIRECTORY`; an existing output directory receives the stable UUID filename. Keep generated `.gtheme` files on Scratch or your player card, outside Git. The [format guide](../../docs/theme-format.md) lists all palette roles and image slots.

The simulator capture runner is `firmware/tools/theme_pack_captures.py`. Build `echo_sim` and `theme_pack_tool` through `tools/firmware.py`, then supply `--sim`, `--state-tool` and a Scratch `--output` directory. It creates both packs, original test audio, all six layouts on Home, Music, Now Playing, Settings and the Theme picker, a contact sheet and simulator frame-time samples.
