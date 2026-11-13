# Echo-sync validation and failure behavior

`tools/echo_sync.py` prepares the v1 library, cover cache and scan fingerprint from a selected card root. Source music is read only. The desktop companion does not currently bundle this Python toolchain.

## File boundaries

Visible media symlinks are refused before scanning. The data directory and cache paths must be ordinary, unredirected paths. Cache writes refuse symlinks, hardlinks, changed file identities and changed parent folders. Hosts with directory-relative opens anchor each cache open to the checked directory descriptor; other hosts recheck the opened file and parent identity before writing. A native FAT card has no symlinks, but these checks also protect granted local folders and test fixtures.

Cover paths read from an existing library must remain relative to the selected root. Cached libraries must have the initial v1 header, the expected ten section layouts and bounded section ranges. Corrupt, unsupported or oversized cached libraries trigger a fresh scan; cached-library reads are limited to 128 MiB. This limit affects reuse of an existing cache, not source music file size.

## Publication and recovery

The writer clears and flushes the header first, completes partial writes, flushes the payload and verifies its bytes before publishing the header. It then flushes and verifies the complete logical record. Readback checks the data being published, not unused preallocated space. Existing allocation and trailing unused bytes remain in place.

A write, flush or verification failure stops publication and attempts to invalidate the header. The player can rebuild a rejected cache. A scan fingerprint is invalidated before library publication and is restored only if the music fingerprint remains complete and unchanged. Keep the card connected until the command finishes, eject safely, and retry a failed operation after resolving its cause. Filesystem errors are reported as command errors, with no automatic retry.

The tests inject failures at every write, seek, flush and read in cache publication, simulate abrupt termination, exercise fsync failures and corrupt readbacks, and verify that accepted records are complete old or new records. They also verify retry recovery and unchanged original audio. Simulated termination establishes the software publication order; physical unplug, controller caching and power-loss behavior still need device acceptance.

## Automated checks

Use Python 3.14 and the wrapper-created tools environment:

```sh
python3 tools/firmware.py test --regex '^tools_unit_tests$' --jobs 2
```

Focused echo-sync tests can also run as standard-library unittest tests with the tools dependencies installed. Temporary files and reports belong on Scratch. The platform tests substitute mount and Windows API calls; they do not access a connected player or establish Windows/Linux hardware acceptance.
