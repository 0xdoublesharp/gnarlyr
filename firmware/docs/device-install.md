# Installing a Gnarlyr build

This covers installing a build you made yourself; there is no released image. Before any install, make sure you can recover the player ([`device-recovery.md`](device-recovery.md)) and charge the battery. In this repository every install on the owner's player needs the owner's approval first (`../../AGENTS.md`).

## What has been done on a player

An earlier build of this project, based on Podium 4.5.5, was installed on the owner's Echo (full size, 4 GB) from macOS with exactly the maskrom steps below, and FiiO's 2.0.0 was written back the same way ([`device-recovery.md`](device-recovery.md)). Gnarlyr itself has not been installed on any player yet.

## Install through maskrom

Maskrom writes the image straight into both firmware slots, below the file system. A wrong command can leave the player unable to start until a later write succeeds. Run only the commands listed here.

Each slot holds the image's first `0x500000` bytes (10,240 sectors of 512 bytes). On the full-size Echo the slots start at rockusb sector 0 and sector 81920 (40 MiB).

`rkdeveloptool-echo` is the patched tool from [`device-recovery.md`](device-recovery.md), step 1. Keep the slot file, the backup and the read-backs in a work folder outside the repository.

1. Build the image and cut the slot contents:

   ```sh
   python3 tools/firmware.py build --preset release
   head -c 5242880 firmware/build/release/GNARLYR.IMG > slot.bin
   shasum -a 256 slot.bin
   ```

   The first command runs from the repository root. Use `--preset device` for the build with the developer rows.

2. Enter maskrom ([`device-recovery.md`](device-recovery.md), step 2).

3. Check the connection. Wait two seconds after `ld` lists the device before `td`:

   ```sh
   rkdeveloptool-echo ld
   rkdeveloptool-echo td
   ```

   Continue only if `ld` shows `Maskrom` and `td` says `Test Device OK.`

4. Read both slot headers. Both files must show `RKnanoFW` at offset `0x1F8`; if either doesn't, stop: the slots are not where this document says.

   ```sh
   rkdeveloptool-echo rl 0 1 head1.bin
   rkdeveloptool-echo rl 81920 1 head2.bin
   ```

5. Back up both firmware slots (the first 80 MiB). Read twice and compare, and keep the file until the new build runs:

   ```sh
   rkdeveloptool-echo rl 0 163840 backup-a.bin
   rkdeveloptool-echo rl 0 163840 backup-b.bin
   cmp backup-a.bin backup-b.bin
   ```

6. Write slot 2, read all of it back and compare. Go on only if `cmp` prints nothing:

   ```sh
   rkdeveloptool-echo wl 81920 slot.bin
   rkdeveloptool-echo rl 81920 10240 readback2.bin
   cmp slot.bin readback2.bin
   ```

7. Write slot 1 the same way:

   ```sh
   rkdeveloptool-echo wl 0 slot.bin
   rkdeveloptool-echo rl 0 10240 readback1.bin
   cmp slot.bin readback1.bin
   ```

8. Restart the player. It prints `Reset Device OK.` and the player starts the new build:

   ```sh
   rkdeveloptool-echo rd
   ```

Don't unplug, and don't let the computer sleep, during a write. If a command fails or hangs, follow Rules and "If it goes wrong" in [`device-recovery.md`](device-recovery.md). If a comparison fails, write that slot again; slot 1 still holds the previous firmware until step 7.

## Install by file copy under FiiO's firmware (not verified)

FiiO's updater scans the top level of internal storage for `ECHOVnnn.IMG` files and takes the highest number, so `GNARLYR.IMG` would have to be copied under such a name. Whether FiiO's 2.0.0 accepts a Gnarlyr image is not known; this project has not tried it. Podium's documentation reports that FiiO's updater installs Podium from a file copy.

## Later updates: Gnarlyr's own updater

File-copy updates have run on the owner's player with earlier developer identities. The canonical `GNARLYR` transition is covered by emulator checks and still needs its own device installation ([`known-issues.md`](known-issues.md)).

1. Connect the player. It shows "Computer found", and two drives appear: internal storage and the SD card.
2. Copy the new image to the top level of internal storage and check it. On macOS: `cp -X firmware/build/release/GNARLYR.IMG /Volumes/<internal drive>/`, then `rm -f /Volumes/<internal drive>/._GNARLYR.IMG` and `shasum -a 256` on both copies.
3. Eject both drives. The player shows "Checking GNARLYR.IMG", then "Installing update" with a percentage, then "Update installed".
4. Unplug and hold Power: the player turns off. Hold Power again to start the new build.

What the updater does:

- It reads `GNARLYR.IMG`, and FiiO's `ECHOVnnn.IMG` names so that FiiO's own image can be restored.
- It accepts only images with the `GNARLYR` brand and FiiO's own Echo image (39,845,892 bytes). It refuses Podium, EchoPod, Echo Mini and every other image.
- It checks the image's shape, rows, entry vector, header copy and CRC before writing, and skips an image that is already installed.
- It writes slot 2 first, then slot 1, and reads every chunk back.
- It deletes the file after a successful install, when the file is not a valid image and when it is already installed.

Installing FiiO's image this way is the file-copy way back to FiiO's firmware ("Going back to FiiO's firmware" in the user guide); both slots then hold FiiO's firmware.

## Moving an installed developer build to the canonical identity

An installed updater accepts the filename and image brand of its own identity. A canonical `GNARLYR.IMG` therefore needs a prepared transition when the installed build uses different identifiers. The transition preserves the existing data before the first canonical boot and uses a matching transport bridge, followed by the ordinary canonical image. Follow [`identity-transition.md`](identity-transition.md); do not substitute a bridge built for another installed identity.

## What to check after an install

- **Start-up:** the splash and the Home title say "Gnarlyr", with Music and Settings (and Now Playing once a track is loaded). Without a library, Music says "No library".
- **Settings → About → Firmware:** the version and the commit you built.
- **Keys:** each button does what the Buttons table in [`user-guide.md`](user-guide.md) says.
- **Power:** a tap turns the screen off and on. Holding it for 2 s switches the player off; with a cable in, it shows the charge screen instead, and holding Power again turns the player back on.
- **USB:** "Computer found" appears and both drives mount.
- **Files:** settings live on internal storage (`/.gnarlyr/settings.bin` and `eq_slots.bin`, created on the first start). On the first start with a card, the player creates `/.gnarlyr` on it with `library.db` (4 MB), `covers.bin` (80 MB), `build.tmp` (24 MB) and its small save files, then scans the card and builds the covers. If there is no room it says "Storage is full".
- **Sound:** playback on 3.5 mm at a low volume first, then the items listed under "Not proven on a player" in [`known-issues.md`](known-issues.md).

## If something goes wrong

| Symptom | Next step |
|---|---|
| Black screen, but the drives appear over USB | Copy a fixed image with the updater |
| Black screen and nothing over USB | Maskrom ([`device-recovery.md`](device-recovery.md)): write FiiO's firmware back, or a fixed build as above |
| "Update not installed" | The file isn't a valid image for this player; rebuild it and copy it again |
| "Update failed" | Copy the file again and retry before turning the player off |
| "USB code missing: reinstall the update" | The USB code could not be loaded from either slot; install again through maskrom |
| A STALL or FAULT screen | Note the values shown, then hold Power for 2 s |
