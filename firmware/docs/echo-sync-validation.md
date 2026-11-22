# Echo-sync validation and failure behavior

`tools/echo_sync.py` prepares the v2 library, cover cache and scan record on the selected card. When both mounts are confirmed to belong to the same USB player, it includes music from internal storage. Source music is read only.

## File boundaries

Visible media symlinks are refused before scanning. The data directory and cache paths must be ordinary, unredirected paths. Cache writes refuse symlinks, hardlinks, changed file identities and changed parent folders. Hosts with directory-relative opens anchor each cache open to the checked directory descriptor; other hosts recheck the opened file and parent identity before writing. A native FAT card has no symlinks, but these checks also protect granted local folders and test fixtures.

With internal storage connected, echo-sync also replaces `fonts/extra.font` there through a temporary file in the same folder, flushed and renamed, refusing a linked data or fonts folder (`docs/architecture.md`, Languages, Extra fonts); the cache checks above do not apply to it.

Cover paths from an existing library must resolve inside their music drive. A leading `"/` routes the path to the verified internal mount. A library with internal music requires both drives for cover updates. Cached libraries must have the v2 header, the expected ten section layouts and bounded section ranges. Corrupt, unsupported or oversized cached libraries trigger a fresh scan; cached-library reads are limited to 128 MiB. This limit affects reuse of an existing cache, not source music file size.

## Publication and recovery

The writer clears and flushes the header first, completes partial writes, flushes the payload and verifies its bytes before publishing the header. It then flushes and verifies the complete logical record. Readback checks the data being published, not unused preallocated space. Existing allocation and trailing unused bytes remain in place.

A write, flush or verification failure stops publication and attempts to invalidate the header. The player can rebuild a rejected cache. Before publishing a library, echo-sync writes a valid `DSS2` record with zero card fingerprint fields and internal storage marked unknown. This tells the player to check at start-up. Echo-sync replaces that record with computed fingerprints only if they are complete and unchanged. It attempts to restore the unknown record if writing the computed fingerprints fails. Keep the card connected until the command finishes, eject safely, and retry a failed operation after resolving its cause. Filesystem errors are reported as command errors, with no automatic retry.

The tests inject failures at every write, seek, flush and read in cache publication, simulate abrupt termination, exercise fsync failures and corrupt readbacks, and verify that accepted records are complete old or new records. They also verify retry recovery and unchanged original audio. Simulated termination establishes the software publication order; physical unplug, controller caching and power-loss behavior still need device acceptance.

## Automated checks

Use Python 3.14 and the wrapper-created tools environment:

```sh
python3 tools/firmware.py test --regex '^tools_unit_tests$' --jobs 2
```

Focused echo-sync tests can also run as standard-library unittest tests with the tools dependencies installed. Temporary files and reports belong on Scratch. The platform tests substitute mount and Windows API calls; they do not access a connected player or establish Windows/Linux hardware acceptance.

## Coverage gate

With the tools runtime requirements installed, add the pinned test dependency from `tools/requirements-test.txt` to the Python 3.14 tools environment. Run from the repository root:

```sh
python3 firmware/tools/echo_sync_coverage.py --output /Volumes/Scratch/SnowskyEcho/reports/echo-sync-coverage
```

The command runs the echo-sync, library, cover, CUE, ReplayGain, metadata-skimming and MP4 chapter tests. It enforces 100% lines and branches independently for all ten runtime modules: `echo_sync`, `library_index`, `cover_export`, `cue_sheet`, `replay_gain`, `fnv`, `identity`, `tag_skim`, `mp4_chapters` and `dsd_metadata`. Unimported modules count as gaps; no production code is excluded. `--measure` writes reports without enforcing the threshold. Output must be outside the source checkout.

Malformed metadata tests include corrupt declared lengths at physical EOF, invalid DSD chunks, MP4 chapter clocks and sample tables, plus deterministic byte corruption and every truncation of generated chapter fixtures. They assert bounded chapter counts, titles and times and unchanged source bytes. CLI tests execute the actual entrypoints against disposable music folders and verify incomplete inputs, unreadable tags, unresolved playlist entries and failure status.

The companion's `pnpm test:echo-sync CHECKOUT --python PYTHON` invokes this gate against a separate checkout without bundling the Python implementation. Its reports remain separate from companion TypeScript coverage.
