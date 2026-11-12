# Card formatting

Settings → Format card operates on the raw SD block device. Its HAL implementation never passes an eMMC device to the formatter. The portable filesystem readers and file creators are reused to mount and populate the result; `fat_create` and `exfat_create` create entries in an existing filesystem and do not create volumes. The seven `created_cards_pass_fsck` images continue to exercise those existing entry operations separately.

## Layout

`device/card_format` uses 512-byte sectors and one MBR partition named GnarlySD. Through 67,108,864 sectors (32 GiB) it creates FAT32 with two FATs, FSInfo and a backup boot sector. Larger cards use exFAT with one FAT, an allocation bitmap, the recommended up-case table, an empty root and both boot regions with checksums. The supported lower bound is 64 MiB; the existing block driver uses a 32-bit sector count.

FAT32 uses 32 KiB clusters above 2 GiB and 16 KiB at 2 GiB. Smaller volumes reduce the cluster size until the FAT32 cluster count remains valid. exFAT uses 128 KiB clusters. FAT32 on standard-capacity SD is a deliberate firmware policy: Gnarlyr uses FAT32 through 32 GB, while the [SD Association capacity definitions](https://www.sdcard.org/developers/sd-standard-overview/capacity-sd-sdhc-sdxc-sduc/) specify FAT16 through 2 GB.

The SD driver reads ACMD13's allocation unit and falls back to the CSD erase unit when SD Status is unavailable. The partition and data area are aligned to a common multiple of that unit and a 4 MiB FAT32 or 16 MiB exFAT boundary. Non-power-of-two units, including 12 and 24 MiB, retain their alignment. The [SD physical-layer specification, section 4.10.2.4](https://www.sdcard.org/cms/wp-content/themes/sdcard-org/dl.php?f=Part1_Physical_Layer_Simplified_Specification_Ver7.10.pdf) defines the allocation-unit codes; FAT32 geometries whose reserved-sector count would exceed its 16-bit field are refused before writing.

The exFAT up-case table comes from the existing macOS-created exFAT fixture. `tools/card_format_tables.py` extracts and verifies its 5,836 bytes and `E619D30D` checksum before generating `platform/device/logic/card_format_upcase.h`. Regenerate with:

```sh
python3 tools/card_format_tables.py --output platform/device/logic/card_format_upcase.h
```

The table and boot checksums follow the [Microsoft exFAT specification](https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification), including the three excluded boot-checksum bytes.

## Write order and state

The first write clears sector 0. Only system areas and the new root are initialized; file contents elsewhere are left alone. The valid MBR is written last, after all filesystem metadata. Failure before that commit leaves the player's normal reader unable to mount the card. This also hides an older boot region left elsewhere on a previously formatted card.

Writes are at most eight sectors, using one fixed 4 KiB buffer. The formatter's state and its 4 KiB buffer borrow the decoder workspace (`board/audio.c`), which a static assertion keeps large enough: `board_audio_lend_format` lends it only when no track, USB DAC stream, output, core-1 program or test tone is active, so formatting refuses to start unless audio has been released, and while it is lent playback and USB DAC streams refuse to start and neither the USB nor the rescan overlay loads into that memory (`device/workspace_loan`, with its own host test). The Settings module keeps only the validation volume in private BSS, because the Settings row also uses it to tell whether an inserted card needs formatting; the code and up-case data are cold too. A callback feeds the watchdog and display service, checks removal, USB ownership and battery power, and updates percentage progress at most four times a second. The job stops after 120 seconds, subject to completion of the current bounded driver operation. The SD transfer-state poll has its own one-second deadline and checks card presence.

Opening the warning records a card-presence generation and capacity. The final confirmation checks both again, refuses active card playback or USB, and requires known battery above 30% or charger power. The second confirmation starts on Cancel. A paused output is released and pending state is flushed before filesystem writes start.

After an attempted format the storage HAL invalidates its mounts and handles. The app reloads storage before its next playback update, without flushing old listening state onto the new filesystem. The new root has none of the old library, queue, resume, bookmark or mixtape files; normal preparation and scanning create fresh state. A failed or removed card falls back through the existing storage selection and reports that the card needs formatting.

## Image checks

`test_card_format` creates sparse Scratch images, mounts them through the firmware reader, checks an empty root, writes and reads a file, exercises FAT32/exFAT interruption points, cancels and retries a job, and checks preservation of data outside the system areas. `formatted_cards_pass_fsck` invokes the native formatter for 2, 16, 32, 64 and 256 GiB, attaches only those images with `hdiutil`, runs macOS fsck before and after an image mount/read/write check, detaches, and deletes each image. Temporary images are required to resolve under `/Volumes/Scratch/SnowskyEcho`.

The ARM scenarios exercise the actual SD initialization and write driver, format a synthetic card, copy and play a WAV, and remove/reinsert a card during formatting. Native app tests cover confirmations, safety refusals, card replacement and cleared playback state. English captures cover all text sizes and orientations; the pseudo-language journey visits both confirmations. These checks establish software behavior; physical-card acceptance is recorded separately by the owner.
