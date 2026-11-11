# USB companion controls

Use the macOS developer helper to inspect and change supported player settings. Connect the ECHO in USB storage mode and leave the cable connected. Each change runs through the normal player setting command, ejects the drive and waits for USB to reconnect.

```sh
python3 tools/echo_debug.py settings-list
python3 tools/echo_debug.py settings-set text_size large
python3 tools/echo_debug.py settings-set orientation landscape
```

`settings-list` returns canonical keys and values together with their current localized labels. Supported display controls are `text_size`, `orientation`, `brightness` and `screen_timeout`. Firmware with the declared version 14 settings layout also exposes `replay_gain` and `prevent_clipping`. Choose values from the returned options.

The helper resolves the exact installed build in local Git history and checks its settings layout, language catalogue and current translation. After a change, it verifies the complete saved-settings file: the requested value and checksum must change as expected, and every other byte must be preserved. An unexpected result is reported without replaying the command.

The stdio MCP server exposes the same operations as `echo_settings_list` and `echo_settings_set`. Set arguments are `setting`, `value`, and optional `music_volume` and `timeout`; list accepts optional `music_volume`. These operations share the CLI implementation.

A missing preferences file or an older saved format requires a normal settings save on the player first. This makes the stored values readable in the installed format. Language-pack inspection, installation and selection use the separate [language commands](language-packs.md).

The transport is USB storage on macOS. These commands do not establish a phone Bluetooth data channel.
