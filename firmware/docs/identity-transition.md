# Moving an installed developer build to Gnarlyr

The canonical identity is `GNARLYR`, with `GNARLYR.IMG`, `/.gnarlyr` and `#GNARLYR-START:`. All ordinary builds, the simulator and host tools read these values from `firmware/identity.json`.

This procedure has completed on the owner's full-size ECHO, including both updater hops and the preserved canonical data copy. The private installation log records its exact images; [`known-issues.md`](known-issues.md) records what was checked on the player.

## Inputs and preparation

Retain the installed build's image, matching ELF files and `identity.json`, along with the exact-model recovery route. The identity file supplies the previous update filename, image brand, data-folder name and playlist directive; do not infer them from the display name.

Build the ordinary canonical developer image with `tools/firmware.py`. Prepare its transport bridge with the existing image packer's `--brand` option, using the installed identity's brand and update filename. The bridge must carry the same canonical code, overlays, decoder programs, modules, fonts and language packs as the ordinary image. Its transport header is the only identity difference. The packer writes the duplicate header and CRC normally.

Both update hops must be checked in the emulator against the retained installed image before device installation. Validate container bounds, header copies, CRC and payload identity as well as slot ordering and write readbacks. The first hop boots canonical code; the second installs its ordinary canonical image.

## Preserve existing music state

Before the first canonical boot, back up each volume's complete existing data folder and prepare its copy at `/.gnarlyr`. Preserve all file bytes, including the library, settings, button/EQ choices, queue/session banks, mixtapes, labels, favorites, bookmarks and listening files. Their binary formats and maker-specific `DS` markers do not change. Keep the original folder and the independent backup until persistence checks pass.

For exported CUE playlists, replace the previous identity's start-directive prefix with `#GNARLYR-START:` while preserving its numeric values and following paths. Preserve allocated length and trailing padding in preallocated playlist files when a shorter directive frees space. Keep original playlist copies in the backup. Ordinary music files need no changes.

## Installation order

Use an agreed device window and the applicable owner approval in addition to the preparation above. File-copy installation remains separate from firmware analysis; these steps never require raw device writes.

1. Stage the verified data copies and playlist changes while storage is owned by the computer.
2. Offer the matching transport bridge under the installed updater's filename. Any restart request for this hop belongs to the installed identity's debug folder.
3. Eject both storage volumes and let the installed updater finish. It writes the secondary slot first and checks every write before the next stage.
4. After canonical code starts, use the canonical host tools and `/.gnarlyr/debug` reports.
5. Install the ordinary `GNARLYR.IMG` and confirm its canonical header and running build.
6. Verify settings, labels, saved mixtapes, queue/resume, playlists, audio and normal USB transfers before retiring the previous on-device data folder.

Preserve the original device recovery backup throughout. Firmware installation is independent of publishing Gnarlyr.com.
