# Recovering a player through maskrom

If the player doesn't start at all, its chip's maskrom mode can still write FiiO's firmware back from a computer over USB. It is also the route this project uses to install builds of Gnarlyr ([`device-install.md`](device-install.md)).

Maskrom writes go straight to internal storage, below the file system. A wrong command can leave the player unable to start until a later write succeeds. Follow the steps exactly, and don't run any command not listed here.

The procedure follows ReChord's Linux debrick guide, the livetrack.club debrick guide and Podium's recovery document. On the owner's Echo (full size, 4 GB) it was used from macOS to read both firmware slots, to install an earlier build of this project, and to write FiiO's V2.0.0 back: the key combination, the USB ID `2207:262d` and the commands in steps 3 and 4 are the ones that were run.

## What you need

- FiiO's firmware for the full-size Echo (`ECHOV200.IMG` for V2.0.0), from FiiO's official firmware page.
- A data USB cable (not charge-only), plugged into a port on the computer itself, not a hub.
- A paperclip for the RST hole.
- The patched `rkdeveloptool` (below). Never use any other `rkdeveloptool` binary: the upstream tool doesn't know this device.

Internal storage may be wiped. If the player still shows its drive over USB, copy off anything you want to keep first.

## Escape hatch: `MASKROM.REQ`

Gnarlyr checks internal storage at every start for a file named `MASKROM.REQ` in the top folder. If it starts with the text `MASKROM REQ`, the player overwrites that text with zeros (so the request fires once) and reboots into the boot ROM's USB loader, the same state as step 2 below, with no paperclip and no key combination. To use it, put the file there while the player is still running (`echo "MASKROM REQ" > MASKROM.REQ` on the internal drive), eject, and restart the player. A file without that text, or an empty one, does nothing.

This is checked in the emulator. This project has not used it on a player.

## Escape hatch: Power and vol+

The same restart into the boot ROM's USB loader works without a file and without USB storage:

1. Plug the cable into the computer (a port on the computer itself). The player can be on, showing a message, or switched off on the cable (the dark charge screen). If the player is fully off, plugging the cable in starts it; that is enough.
2. Hold **Power** and **vol+** together, and keep holding them for 5 s.
3. After 3 s with both keys held and the cable powered, the player restarts into maskrom with the screen black. Let go, then run `rkdeveloptool-echo ld` (step 3 below).

Gnarlyr checks the keys every 5 ms for as long as it runs, from the first key scan at start-up, including at messages, on the charge screen and during a USB session. Without cable power, with only one of the keys, or with a key let go before 3 s, nothing happens. Two ladder keys can't be told apart on this player, so the combination uses Power plus one ladder key. Holding vol+ may raise the volume while music plays.

If the player starts Gnarlyr again instead of staying black, the restart didn't reach maskrom; the keys then do nothing until Power is let go. In a `device` build, connect it as a drive once: `/.gnarlyr/debug/report0.txt` then says `start.last_step=loader`, which tells the developer the restart was requested but the boot ROM didn't stay in the loader. Use the paperclip route (step 2 below). If nothing happens at all, Settings → About → Keys shows the key reading in a `device` build: vol+ alone should read between 70 and 183.

The emulator checks the timing and the restart request. On the player, the earlier start-up-only check failed once (`known-issues.md`); this version is unchecked on a player. FiiO's own firmware for this player writes the same loader request before it restarts. It needs a build that gets as far as its key scanning.

## 1. Build the patched `rkdeveloptool` (once)

The upstream tool needs four changes for this device: a 5000 ms command timeout, 32 sectors per transfer, maskrom detection for USB ID `2207:262d`, and the chip version read from that device's `bcdDevice`. `tools/patch_rkdeveloptool.py` makes them on upstream revision `304f073752fd25c854e1bcf05d8e7f925b1f4e14`. It prints `ok: Echo changes applied`, and refuses a source it doesn't recognise or one that is already patched.

### This repository, macOS

From the repository root, with the Scratch volume mounted:

```sh
export TMPDIR=/Volumes/Scratch/SnowskyEcho/tmp/recovery-tool
mkdir -p "$TMPDIR"
git clone https://github.com/rockchip-linux/rkdeveloptool.git .local/references/rkdeveloptool
git -C .local/references/rkdeveloptool checkout --detach 304f073752fd25c854e1bcf05d8e7f925b1f4e14
python3 firmware/tools/patch_rkdeveloptool.py .local/references/rkdeveloptool
cmake -S tools/recovery -B /Volumes/Scratch/SnowskyEcho/build/recovery-tool \
  -DRKDEVELOPTOOL_SOURCE="$PWD/.local/references/rkdeveloptool" -DCMAKE_BUILD_TYPE=Release
cmake --build /Volumes/Scratch/SnowskyEcho/build/recovery-tool -j4
```

The CMake project in `../../tools/recovery` verifies the revision and the patched contents before it builds, uses the installed libusb, and installs nothing. The result is `rkdeveloptool-echo`, version 1.32, in that build folder. This is the tool that was used on the owner's player. Clone and patch once; do not patch a checkout a second time.

### Other computers

These steps are from Podium's recovery document. This project has not tried them. They build the same patched source under its upstream name, `rkdeveloptool`; use that name in the commands below.

Windows: install [MSYS2](https://www.msys2.org) with the CLANG64 packages `mingw-w64-clang-x86_64-{clang,compiler-rt,autotools,libusb}` and `git`. Then, in an MSYS2 CLANG64 shell, from the `firmware` folder:

```sh
mkdir -p external && cd external
git clone https://github.com/rockchip-linux/rkdeveloptool.git
cd rkdeveloptool
git checkout 304f073752fd25c854e1bcf05d8e7f925b1f4e14
python ../../tools/patch_rkdeveloptool.py .
autoreconf -fi
mkdir -p build-clang64 && cd build-clang64
../configure --prefix=/clang64
make AM_CPPFLAGS='-Wall -Wextra -Wreturn-type -fno-strict-aliasing -D_FILE_OFFSET_BITS=64 -D_LARGE_FILE $(LIBUSB1_CFLAGS)'
./rkdeveloptool -v
```

The `make` line drops upstream's `-Werror`. The tool needs `C:\msys64\clang64\bin` on the PATH when it runs.

macOS with autotools instead of CMake:

```sh
brew install libusb automake autoconf pkgconf
mkdir -p external && cd external
git clone https://github.com/rockchip-linux/rkdeveloptool.git
cd rkdeveloptool
git checkout 304f073752fd25c854e1bcf05d8e7f925b1f4e14
python3 ../../tools/patch_rkdeveloptool.py .
./autogen.sh && ./configure && make CXXFLAGS="-g -O2 -Wno-vla-cxx-extension"
./rkdeveloptool -v
```

## 2. Enter maskrom

1. Unplug USB. Press the **RST** hole with the paperclip so the player is fully off. Don't use the Power button.
2. Hold **every button except Power**: ▶‖, ≡, ↩ and the volume rocker.
3. Still holding, plug the USB cable in. Let go after 2–3 seconds.
4. The screen stays completely black. That's expected.

### Windows only: bind maskrom to WinUSB (once per computer)

From Podium's recovery document; not tried by this project.

1. With the player in maskrom, Windows lists `USB\VID_2207&PID_262D` with no driver.
2. Run [Zadig](https://github.com/pbatard/libwdi/releases) (signed by Akeo Consulting) and allow the admin prompt.
3. Options → List All Devices. Pick the entry whose USB ID reads `2207` `262D` (it shows as "Unknown Device #1"). Check both numbers: replacing another device's driver breaks that device.
4. Set the target to WinUSB and click Install Driver.

Only maskrom mode uses this ID, so the player's normal USB drives are unaffected.

## 3. Check the connection

```sh
rkdeveloptool-echo ld
rkdeveloptool-echo td
```

Wait two seconds after `ld` lists the device before running `td`. Continue only if `ld` shows `Maskrom` and `td` says `Test Device OK.`

If the player still holds firmware you may want back, read the first 80 MiB (both firmware slots) twice, compare, and keep the file:

```sh
rkdeveloptool-echo rl 0 163840 backup-a.bin
rkdeveloptool-echo rl 0 163840 backup-b.bin
cmp backup-a.bin backup-b.bin
```

## 4. Write FiiO's firmware

FiiO's image goes into both firmware slots, at sector 81920 and at sector 0, whole and unchanged. Each write is read back and compared before the next step.

1. Check the image against the hash FiiO publishes (for V2.0.0 `ECHOV200.IMG`, `c6a3b6c86b141c0a428771f8a611fbd44cc12d561acb033a98640b0bf24f8d57`). The full-size Echo's image is 39,845,892 bytes, which is 77,825 sectors with the last one partly used, so the comparison below takes only the image's bytes from each read-back.

2. Write slot 2, read it back and compare the image's bytes. Go on only if `cmp` prints nothing:

   ```sh
   rkdeveloptool-echo wl 81920 ECHOV200.IMG
   rkdeveloptool-echo rl 81920 77825 readback2.bin
   head -c 39845892 readback2.bin | cmp - ECHOV200.IMG
   ```

3. Write slot 1 the same way:

   ```sh
   rkdeveloptool-echo wl 0 ECHOV200.IMG
   rkdeveloptool-echo rl 0 77825 readback1.bin
   head -c 39845892 readback1.bin | cmp - ECHOV200.IMG
   ```

4. Restart the player:

   ```sh
   rkdeveloptool-echo rd
   ```

   `rd` prints `Reset Device OK.`

5. The player starts FiiO's firmware. If it stays black, unplug USB, press RST, then press Power. Settings → About should show the version you wrote.

`wl` writes the image in small transfers and prints progress up to 100%. Don't unplug, and don't let the computer sleep, until it finishes.

Podium's recovery document writes the image once, with `wl 0 ECHOV200.IMG` and no read-back, and reports that this brought FiiO's V2.0.0 back on its author's Echo. This project has not tried the single write.

Before you write, make sure no `ECHOVnnn.IMG`, `ECHOECHO.IMG` or `GNARLYR.IMG` file is left at the top level of internal storage if you can still see the drive. FiiO's firmware installs a file with one of its own names at its first start, and that would put the old image straight back.

## Rules

- Never run `db`: Podium's document says it fails on this boot ROM and can wedge USB. Never write anything except a whole FiiO image or the slot file of [`device-install.md`](device-install.md): no `ul`, `ef`, `wlx`, unpacked parts or RKAF tools.
- Never write outside the two firmware slots. The only write addresses in these documents are sector 81920 and sector 0.
- If a command fails or hangs (more than about a minute with no progress), the session is wedged. Press Ctrl+C, unplug, press RST, enter maskrom again and start over from `ld`.
- If `wl` fails twice the same way, try another USB port, cable or computer.
- A failed `wl` can leave the player unable to start until a later `wl` succeeds.

## If it goes wrong

| Symptom | Try |
|---|---|
| Nothing with `2207:262d` appears | Repeat step 2. Use a data cable and a port directly on the computer. |
| `ld` says `Loader`, not `Maskrom` | The patch didn't apply. Rebuild the tool. |
| `ld` lists nothing on Windows | Bind the device to WinUSB with Zadig (step 2). |
| `td` hangs or times out | Unplug, press RST, and start again. On Linux, disable USB autosuspend first. |
| A read-back differs | Write that slot again and compare again. Don't run `rd` until both slots compare equal. |
| The player won't start after `rd` | Press RST, wait 10 s, then press Power. |
