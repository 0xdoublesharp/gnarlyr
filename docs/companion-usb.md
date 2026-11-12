# USB companion controls

Use the macOS developer helper to inspect and change supported player settings. Plug in the ECHO, choose Connect to computer and leave the cable connected. Each change runs through the normal player setting command, ejects the drive and waits for USB to reconnect.

```sh
python3 tools/echo_debug.py settings-list
python3 tools/echo_debug.py settings-set text_size large
python3 tools/echo_debug.py settings-set orientation landscape
```

`settings-list` returns canonical keys and values together with their current localized labels. Supported display controls are `text_size`, `orientation`, `brightness` and `screen_timeout`. It also exposes `replay_gain` (with `auto` where the build offers it), `prevent_clipping`, `clock_24h` and the status bar items `status_volume`, `status_repeat`, `status_position`, `battery_style` and `battery_percent`. The helper accepts only the version 1 settings layout ([formats](formats.md)): it checks the extension block (bytes 38–53), the second block (bytes 58–73) and both checksums, and keeps both blocks unchanged when it changes a setting. The Home order in the block (bytes 42–46) and its hidden mask (bytes 47–48) are checked by the firmware's rule: the stored order is a permutation of 0..L−1 in its leading nibbles, with the rest zero, where L is at least 9; only stored items other than Settings (item 3) may be hidden. The order may hold more or fewer items than the installed build has (`SETTINGS_HOME_ITEM_COUNT`): the firmware drops items it does not know, keeping the rest in order, and appends its own missing items at the end, visible. Choose values from the returned options.

The helper resolves the exact installed build in local Git history and checks its settings layout, language catalogue and current translation. After a change, it verifies the complete saved-settings file: the requested value and checksum must change as expected, and every other byte must be preserved. An unexpected result is reported without replaying the command.

The stdio MCP server exposes the same operations as `echo_settings_list` and `echo_settings_set`. Set arguments are `setting`, `value`, and optional `music_volume` and `timeout`; list accepts optional `music_volume`. These operations share the CLI implementation.

A missing settings file, or one of another version, requires a normal settings save on the player first; the player replaces a refused file with its defaults. Language-pack inspection, installation and selection use the separate [language commands](language-packs.md).

## Clock

Every command that ejects the player's drives (`install`, `run`, `settings-set` and the language commands) first writes this computer's time, in UTC with the local offset from UTC, as `.gnarlyr/time.bin` on the internal drive. The player sets its clock from that file when the drive is ejected and then deletes it. To set the clock on its own:

```sh
python3 tools/echo_debug.py set-time
```

`set-time` writes the file and ejects both drives; it returns the time it sent. The MCP server offers it as `echo_set_time`, with no arguments. A computer clock the player would refuse (before 2025, after 2099, or an offset outside −12 to +14 hours) fails `set-time` without writing or ejecting; the other commands then go on without the time and say so in `time_skipped`. With `--volume PATH`, `set-time` writes the file into that folder and ejects nothing.

The player applies the time when it sees the eject, so the clock runs a few seconds behind this computer's. A file that is still there after a session ended by pulling the cable, or when the player starts, is deleted without being applied, so an old time never sets the clock. The time is kept until the player is switched off or restarts; the plays it logs meanwhile carry it. The file format is in the firmware's architecture notes (Wall clock).

The transport is USB storage on macOS. These commands do not establish a phone Bluetooth data channel.
