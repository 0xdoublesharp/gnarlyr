import argparse
import json
import struct
from pathlib import Path

import echo_sync
import image_artifacts
import library_index
import make_image
from identity import IDENTITY
from echo_emulator.audio import AudioPath
from echo_emulator.fat_volume import Fat32Volume
from echo_emulator.playback import Rig
from echo_emulator.session import Session
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R2


class Wave:
    def __init__(self):
        self.size = 44100 * 4 + 44
        self.header = struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', self.size - 8,
                                  b'WAVE', b'fmt ', 16, 1, 2, 44100, 176400, 4, 16,
                                  b'data', self.size - 44)

    def read(self, offset, length):
        return bytes(self.header[index] if index < 44 else (index & 3) * 16
                     for index in range(offset, offset + length))


def volume_with_library(tracks, version):
    volume = Fat32Volume(1024, 8, fsinfo=True)
    volume.add_directory(IDENTITY.data_directory)
    entries = []
    wave = Wave()
    for index in range(tracks):
        name = 'T%04d.wav' % index
        volume.add_file(name, source=wave)
        entries.append(library_index.TrackInfo(
            path='/' + name, title='Track %04d' % index, artist='Artist',
            album_artist='Artist', album='Album %03d' % (index // 20), genre='Test',
            year=2026, disc=1, number=index % 20 + 1, duration_ms=1000))
    if tracks:
        database = library_index.encode_library(library_index.build_library(entries))
        volume.add_file('library.db', database, 4 * 1024 * 1024, IDENTITY.data_directory)
        stamp = echo_sync.SCAN_RECORD.pack(echo_sync.SCAN_MAGIC, 0, 0, 0, version.encode('ascii')[:15])
        volume.add_file('scan.bin', stamp, 4096, IDENTITY.data_directory)
    for name, size in (('covers.bin', 80 * 1024 * 1024), ('build.tmp', 24 * 1024 * 1024)):
        volume.add_file(name, size=size, directory=IDENTITY.data_directory)
    return volume


def measure(image_path, tracks):
    image_path = Path(image_path)
    build = image_path.parent
    image = image_path.read_bytes()
    elf = build / 'platform/device/echo_device.elf'
    tag = image[make_image.BUILD_TAG_OFFSET:make_image.BUILD_TAG_OFFSET + 8].rstrip(b'\0').decode('ascii')
    version = IDENTITY.version + '-' + tag
    session = Session(image, elf, volume=volume_with_library(tracks, version),
                      usb_overlay_elf=elf.with_name('echo_usb_overlay.elf'),
                      module_elfs=image_artifacts.module_elfs(build))
    machine = session.machine
    audio = AudioPath(machine)
    phases = []
    watched = ('board_main', 'board_display_start', 'start_app', 'library_store_open',
               'player_restore', 'image_crc_update', 'board_overlay_load',
               'board_glyphs_start', 'cover_cache_open', 'bluetooth_link_resume',
               'home_art_update', 'cover_cache_load_full', 'card_files_missing',
               'card_files_reserve', 'render')
    for prefix in watched:
        names = [name for name in machine.symbols
                 if name == prefix or name.startswith(prefix + '.')]
        for name in names:
            pending = {}
            returns = set()

            def enter(name=name, pending=pending, returns=returns):
                row = {'name': name, 'start_us': machine.microseconds(),
                       'bytes': machine.reg(UC_ARM_REG_R2) if name == 'image_crc_update' else None}
                phases.append(row)
                address = machine.reg(UC_ARM_REG_LR) & ~1
                pending[address] = row
                if address in returns:
                    return
                returns.add(address)

                def returned():
                    active = pending.pop(address, None)
                    if active is not None:
                        active['end_us'] = machine.microseconds()

                machine.add_address_hook(address, returned)

            machine.hook_symbol(name, enter)
    steps = machine.trace('app_step')
    interactive = []
    fade_returns = set()
    fade_pending = set()

    def fade():
        address = machine.reg(UC_ARM_REG_LR) & ~1
        fade_pending.add(address)
        if address in fade_returns:
            return
        fade_returns.add(address)

        def returned():
            if address not in fade_pending:
                return
            if steps and session.panel.lit_sweeps() and not machine.reg(UC_ARM_REG_R0):
                interactive.append(machine.microseconds())
            fade_pending.discard(address)

        machine.add_address_hook(address, returned)

    machine.hook_symbol('fade_running', fade)
    first_reason = session.run_to_first_frame()
    first_us = machine.microseconds()
    reason = machine.run(until=lambda: bool(interactive), until_ms=20000)
    ready_us = interactive[0] if interactive else None
    rig = Rig.__new__(Rig)
    rig.session, rig.machine, rig.audio, rig.steps = session, machine, audio, steps
    if tracks and ready_us is not None:
        rig.settle()
        rig.open_songs()
        play_ms = rig.play_song(1)
        first_audio = rig.first_audio_after(play_ms)
    else:
        play_ms, first_audio = None, None
    parts = make_image.split_image(image)
    loaded = sum(parts.rows[index][2] + parts.rows[index + 1][1]
                 for index in range(0, len(parts.rows) - 1, 2))
    hold = next((cycle for cycle, port, _, _, data, direction, _ in machine.gpio_log
                 if port == 2 and data & direction & (1 << 6)), None)
    marker_names = ('clock', 'display', 'glyphs', 'app', 'frame', 'extension', 'interactive', 'audio')
    markers = (struct.unpack('<8I', machine.read_memory(machine.symbols['boot_us'], 32))
               if 'boot_us' in machine.symbols else (0,) * 8)
    return {'tracks': tracks, 'first_frame_us': first_us, 'interactive_us': ready_us,
            'power_hold_cycles_24mhz': hold, 'power_hold_us': hold / 24 if hold is not None else None,
            'rom_load_bytes': loaded, 'image_bytes': len(image), 'scatter_rows': parts.rows,
            'play_ms': play_ms, 'audio_ms': first_audio,
            'play_to_audio_ms': first_audio - play_ms if first_audio is not None else None,
            'first_frame_result': first_reason, 'interactive_result': reason,
            'faults': machine.faults, 'panics': machine.panics,
            'watchdog_resets': machine.watchdog_resets, 'phases': phases,
            'boot_markers_us': {name: value - 1 if value else None
                                for name, value in zip(marker_names, markers)},
            'module_reads': {overlay.name: sum(count for _, lba, count in session.card.reads
                                              if overlay.image_offset // 512 <= lba - session.layout['slot_1']
                                              < overlay.image_offset // 512 + (len(overlay.stored) + 511) // 512)
                             for overlay in machine.modules}}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--tracks', type=int, choices=(0, 4000), required=True)
    arguments = parser.parse_args()
    result = measure(arguments.image, arguments.tracks)
    print(json.dumps(result, indent=2))
    if (result['faults'] or result['panics'] or result['watchdog_resets']
            or result['interactive_us'] is None
            or (arguments.tracks and result['audio_ms'] is None)):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
