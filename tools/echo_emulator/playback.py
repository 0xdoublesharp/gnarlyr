from .audio import AudioPath
from .real_volume import real_volume

KEYS = {"play": 250, "menu": 350, "back": 500, "previous": 35, "next": 100}
LONG_PRESS_MS = 1300
QUIET_MS = 300
SETTLE_LIMIT_MS = 4000
MUSIC_ENTRIES_BEFORE_SONGS = 6
ALBUM_COUNT = 2


class Rig:
    def __init__(self, session_module, image, elf):
        self.session = session_module.Session(image, elf, volume=real_volume(playable=True, album_count=ALBUM_COUNT))
        self.machine = self.session.machine
        self.audio = AudioPath(self.machine)
        self.steps = self.machine.trace("app_step")
        self.session.run_to_first_frame()
        self.session.run_to_home_screen()
        self.settle(1000)

    def settle(self, quiet=QUIET_MS):
        machine = self.machine
        start = machine.milliseconds()
        last = [len(machine.panel.groups), start]

        def done():
            now = machine.milliseconds()
            if len(machine.panel.groups) != last[0]:
                last[:] = [len(machine.panel.groups), now]
            return now - last[1] >= quiet or now - start > SETTLE_LIMIT_MS

        machine.run(until=done)

    def press(self, name, hold=150):
        start = self.machine.milliseconds()
        self.machine.press_key(KEYS[name], delay_ms=20, hold_ms=hold)
        self.machine.run(until_ms=start + 20 + hold + 50)
        self.settle()
        return start + 20

    def press_many(self, name, count):
        for _ in range(count):
            self.press(name)

    def open_songs(self):
        self.press("play")
        self.press_many("next", MUSIC_ENTRIES_BEFORE_SONGS)
        self.press("play")

    def play_song(self, number):
        self.press_many("next", number - 1)
        return self.press("play")

    def skip(self, name):
        start = self.machine.milliseconds()
        self.machine.press_key(KEYS[name], delay_ms=20, hold_ms=LONG_PRESS_MS)
        self.machine.run(until_ms=start + 20 + LONG_PRESS_MS + 100)
        self.settle()
        return start + 20

    def scroll(self, name, count):
        start = self.machine.milliseconds()
        for _ in range(count):
            self.machine.press_key(KEYS[name], delay_ms=10, hold_ms=100)
            self.run_for(160)
        return start

    def run_for(self, milliseconds):
        self.machine.run(until_ms=self.machine.milliseconds() + milliseconds)

    def longest_gap(self, since, until=None):
        stamps = [stamp for stamp in self.steps if stamp >= since and (until is None or stamp <= until)]
        gaps = [later - earlier for earlier, later in zip(stamps, stamps[1:])]
        return max(gaps) if gaps else 0

    def first_audio_after(self, stamp):
        later = [value for value in self.audio.audible if value >= stamp]
        return later[0] if later else None

    def fat_sector_reads(self, since, until):
        session = self.session
        first = session.layout["volume_start"] + 32
        last = first + session.volume.fat_sectors
        return sum(count for stamp, lba, count in session.card.read_log
                   if since <= stamp < until and first <= lba < last)

    def data_sector_reads(self, since, until):
        session = self.session
        first = session.layout["volume_start"] + session.volume.data_start
        return sum(count for stamp, lba, count in session.card.read_log
                   if since <= stamp < until and lba >= first)
