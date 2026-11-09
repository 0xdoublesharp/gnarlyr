# Source foundation

Imported from <https://github.com/swap/podium> at revision `bfe1716e9ac36cb471a1e1a558719b6b2b4b3f6b`, including its GPL-3.0-or-later license and credits. This is existing open source, not original vendor source recovered from the stock image. It targets the full-size SNOWSKY ECHO; the inherited EchoPod names also appear in portable code.

Local build changes use FFmpeg from PATH and permit Scratch-backed build/venv symlinks. Use Python 3.14: its Unicode 16 tables reproduce the checked-in sort tables. Run the project-level `tools/firmware.py` wrapper from the parent directory to keep generated output on Scratch. The parent project documentation records which stock interfaces REA confirmed and which functions still need verification on the owner's device.

Upstream documentation describes its author's testing. It is not a test record for this device. Device write commands in inherited documentation require the parent project's exact-model recovery and deployment checks first.

Documentation photos remain upstream; README image links point to that pinned revision. They are not committed as local evidence.

Test fixtures retain intentional trailing fields, spaces and CRLF/BOM text; third-party source is preserved verbatim. These paths are excluded from whitespace cleanup.

Modified 2026-11-09 for this project: cassette-style palette/artwork, configurable playback controls, native-safe versioned preferences migration and isolated image-emulator registration. These changes are tracked in the parent project; inherited hardware claims still require verification on the owner's unit.
