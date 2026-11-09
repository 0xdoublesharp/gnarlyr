# Installing development builds

This covers installing builds you made yourself. Release users should follow `docs/user-guide.md`. Before trying a build of your own, make sure you can recover the player (`docs/device-recovery.md`).

## First install

Copy the image to the drive root under FiiO's firmware, eject and unplug: FiiO's updater installs it (`docs/user-guide.md`, Install). If it shows "Updating" briefly and starts FiiO's firmware again, nothing was written; copy it again with plenty of free space, or use a direct maskrom write, below.

### A direct maskrom write

Maskrom writes the image straight into both firmware slots. Each slot holds the image's first `0x500000` bytes. On the full-size Echo the slots are at rockusb sector 0 and 81920 (40 MiB). Check yours by reading both headers first (`rl 0 1` and `rl 81920 1` must both show `RKnanoFW` at `0x1F8`).

1. Build and cut the slot contents:

   ```sh
   cmake --preset device && cmake --build --preset device
   head -c 5242880 build/device/ECHOV455.IMG > build/device/slot.bin
   ```

2. Enter maskrom (`docs/device-recovery.md`, step 2).
3. Back up both firmware slots (the first 80 MiB, about 20 s) and keep the file until Podium runs:

   ```sh
   cd external/rkdeveloptool
   ./rkdeveloptool ld && ./rkdeveloptool td
   ./rkdeveloptool rl 0 163840 echo-flash-80MiB.bin
   ```

4. Write slot 2, then slot 1, and read both back:

   ```sh
   ./rkdeveloptool wl 81920 ../../build/device/slot.bin
   ./rkdeveloptool wl 0 ../../build/device/slot.bin
   ./rkdeveloptool rl 0 1 s1.bin && ./rkdeveloptool rl 81920 1 s2.bin
   ```

   Both read-backs must equal the first 512 bytes of `slot.bin`.
5. `./rkdeveloptool rd`. The player starts Podium.

## Later updates: Podium's own updater

1. Connect the player. It shows "Computer found", and two drives appear: internal storage and the SD card.
2. Copy the new image to the top level of internal storage and check it:
   - Windows (PowerShell): `Copy-Item build\device\ECHOV455.IMG E:\` then `Get-FileHash E:\ECHOV455.IMG`, with E: the internal drive.
   - macOS: `cp -X build/device/ECHOV455.IMG /Volumes/<internal drive>/`, then `rm -f /Volumes/<internal drive>/._ECHOV455.IMG` and `shasum -a 256` on both copies.
3. Eject both drives. The player shows "Checking ECHOV455.IMG", then "Installing update" with a percentage, then "Update installed".
4. Unplug, hold Power to switch off, then switch on. The new build starts.

The updater takes the highest-numbered `ECHOVnnn.IMG` (`ECHOECHO.IMG` first). It accepts only images with the `PODIUM` brand and refuses EchoPod, Echo Mini and RECHO images. It checks the image's shape, rows, entry vector, header copy and CRC before writing, skips an image that is already installed, and writes slot 2 first, then slot 1, reading back every chunk. It deletes the file after a successful install, when the file is not a valid image and when it is already installed. It also accepts FiiO's own Echo image (39,845,892 bytes; it contains `ECHOV100IMG`) under one of those names, which is the way back to FiiO's firmware ("Going back to FiiO's firmware" in the user guide); both slots then hold FiiO's firmware.

## What to check after an install

- **Home screen:** "EchoPod" with Music, Now Playing, Settings and Diagnostics. Without a card, Music says "No library".
- **Diagnostics → Firmware:** the commit you built.
- **Keys:** Diagnostics → Key test.
- **Power:** a tap turns the screen off and on. Holding it for 2 s switches the player off; with a cable in, it shows the charge screen instead, and holding Power again turns the player back on.
- **USB:** "Computer found" appears and the drives mount within about 10 s.
- **Card:** settings live on internal storage (`/.echo/prefs5.bin`, created on the first start; legacy `settings.bin` is retained). On the first start with a card, the player creates `/.echo` with `resume.bin`, `favorites.bin`, `scan.bin`, `upnext.bin`, `bluetooth.bin`, `library.db` (4 MB), `covers.bin` (80 MB) and `build.tmp` (24 MB), then scans the card and builds the covers. If the card has no room it says "The card is full", and nothing is saved.

## If something goes wrong

| Symptom | Next step |
|---|---|
| Black screen, but the drives appear over USB | Copy a fixed image with the updater |
| Black screen and nothing over USB | Maskrom recovery (`docs/device-recovery.md`), then a direct maskrom write of a fixed image |
| "Update not installed" | The file isn't a valid image for this player; rebuild it and copy it again |
| "Update failed" | Copy the file again and retry before turning the player off |
| A STALL or FAULT screen | Note the values shown, then hold Power for 2 s |
