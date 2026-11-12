# Installing a Gnarlyr build

This covers installing a build you made yourself; there is no released image. Before any install, make sure you can recover the player ([`device-recovery.md`](device-recovery.md)) and charge the battery. In this repository every install on the owner's player needs the owner's approval first (`../../AGENTS.md`).

## What has been done on a player

An earlier build of this project, based on Podium 4.5.5, was installed on the owner's Echo (full size, 4 GB) from macOS with exactly the maskrom steps below, and FiiO's 2.0.0 was written back the same way ([`device-recovery.md`](device-recovery.md)). Canonical Gnarlyr developer firmware has also been installed through the two-hop file-copy transition, with automatic restart/reconnect and live build confirmation. See [`known-issues.md`](known-issues.md) for the bounded device checks.

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

File-copy updates have run on the owner's player with earlier developer identities. The canonical `GNARLYR` transition has completed on the owner's full-size ECHO after exact-image emulator checks ([`known-issues.md`](known-issues.md)). Trial updates, described next, have run only in the emulator.

1. Connect the player. It shows "Computer found", and two drives appear: internal storage and the SD card.
2. Copy the new image to the top level of internal storage and check it. On macOS: `cp -X firmware/build/release/GNARLYR.IMG /Volumes/<internal drive>/`, then `rm -f /Volumes/<internal drive>/._GNARLYR.IMG` and `shasum -a 256` on both copies.
3. Eject both drives. The player shows "Checking GNARLYR.IMG", then "Installing update" with a percentage, then "Update installed".
4. Unplug and hold Power: the player turns off. Hold Power again to start the new build.
5. Connect again and eject both drives. The player shows "Saving this version as the backup" and copies the new build into slot 2. With `tools/echo_debug.py install --restart`, the next command that ejects (`set-time`, `run`) does this.

What the updater does:

- It reads `GNARLYR.IMG`, and FiiO's `ECHOVnnn.IMG` names so that FiiO's own image can be restored.
- It accepts only images with the `GNARLYR` brand and FiiO's own Echo image (39,845,892 bytes). It refuses Podium, EchoPod, Echo Mini and every other image.
- It checks the image's shape, rows, entry vector, header copy and CRC before writing, and skips an image that is already in slot 1.
- It writes a Gnarlyr image into slot 1 only and reads every chunk back. Slot 2 keeps the build that was running, the known-good build.
- It deletes the file after a successful install, when the file is not a valid image and when it is already installed.

### The trial

The new build is on trial until a USB session with it ends with every drive ejected. Then it copies itself into slot 2 and the trial ends. The record is `/.gnarlyr/trial.bin` on internal storage; leave it alone.

- Every start of the trial build counts one attempt, and every USB session counts one more as it starts. A start after which the player runs for 20 s is not counted.
- At 3 attempts the next start copies slot 2 back into slot 1 before anything is drawn (the screen stays dark for some seconds), restarts, and the previous build says "Update rolled back".
- While the trial build is stuck, for example in a USB session that never connects, holding Power for 10 s restarts the player. Three such tries roll the update back with no paperclip.
- The rollback is done by the firmware copying slot 2; it doesn't depend on how the boot ROM chooses a slot. A build that crashes before its start-up code reaches the trial check can't count its starts; that still needs maskrom.
- A new update installed during a trial replaces the trial build in slot 1; slot 2 is untouched.

After a rollback, `/.gnarlyr/trial_diag.bin` on internal storage holds the failed build's last trial record with its USB and eMMC diagnostics; read it with `xxd` (layout: `docs/architecture.md`, Trial diagnostics). To keep the snapshot of a session that broke storage, restart the stuck build by holding Power for 10 s rather than letting the battery or a power cut end it: the snapshot survives a warm restart, not a power cut.

`docs/architecture.md` (Firmware slots and the boot ROM, Trial updates) has the details and the evidence about the boot ROM.

### Both slots at once (developer option)

To write both slots the old way, slot 2 then slot 1 with no trial, also put a file `BOTHSLOT.REQ` that starts with the text `BOTH SLOTS` at the top level of internal storage: `printf 'BOTH SLOTS\n' > /Volumes/<internal drive>/BOTHSLOT.REQ`. The updater deletes it after the install. With both slots holding a new build there is no backup on the player: a broken build then needs maskrom. FiiO's image is always written into both slots, and so is any image while slot 2 holds no valid firmware.

Installing FiiO's image this way is the file-copy way back to FiiO's firmware ("Going back to FiiO's firmware" in the user guide); both slots then hold FiiO's firmware.

### The first install of a build with trial updates

The updater that runs is the one already installed. A build without trial updates writes the new build into both slots, so the install of the first build with trial updates is not protected by it. Prove it in the emulator first (`firmware/tools/prove_upgrade.py --old <installed> --new <candidate>`), and have maskrom ready.

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
| "USB missing: reinstall update" | The USB code could not be loaded from either slot; install again through maskrom |
| "Update rolled back" | The trial build did not confirm; the previous build is back in both slots. Fix the build and copy it again |
| Stuck during a trial, for example in a USB session | Hold Power for 10 s; the third restart rolls the update back |
| No USB drives, but the player starts | Plug in the cable while holding Power and vol+ for 2 s: the player restarts into maskrom ([`device-recovery.md`](device-recovery.md)) |
| A STALL or FAULT screen | Note the values shown, then hold Power for 2 s |

## Timing a cold start

Use an installed developer image with a verified build tag and the full-size Echo recovery route available. Installation is a separate approved action. Timing starts with the player completely powered off and the cable removed; leaving charge mode is a separate measurement.

1. Keep the library and startup-screen choice fixed. Record the track count, output, Bluetooth choice, resume state and whether this is the first start after an update. A fresh or changed card may need a scan.
2. Film the Power key and display together at 120 fps or faster. Hold Power from completely off. Count frames from the press to the first lit screen, then to Home or Now Playing accepting a key. Record the release too, so a required hold can be distinguished from screen delay.
3. With a paused track restored, press Play and record the time to sound through the selected output at a safe volume. Repeat with the other output separately. Use an audio recording aligned to the video for the audible boundary.
4. Connect USB after the measurement and read the existing debug report through `python3 tools/echo_debug.py report`. Keep the report on Scratch. Confirm its build tag and inspect the eight `boot.*_us` fields. Subtract successive markers for firmware phase costs; the interval before `boot.clock_us` is outside that clock's coverage. `boot.audio_us` marks digital transfer startup, so compare it with the recorded sound.
5. Repeat at least five cold boots with the indexed 4,000-track library, then with no library. Keep first-use scans separate from routine indexed boots. Report the median and range of each boundary.
6. Separately switch off with a cable attached. Check that Hold needs two seconds and Press leaves charging on a tap; verify that connecting a charger or computer still follows the existing charge and USB choices. Return to Hold if accidental presses are undesirable.

The emulator timing command is documented in the architecture's Cold-start path. It cannot measure the interval from the physical key through ROM loading, nor establish that a shorter press keeps the player powered. No timing command above installs firmware.
