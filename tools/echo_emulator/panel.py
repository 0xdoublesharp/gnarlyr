CASET = 0x2A
RASET = 0x2B
RAMWR = 0x2C
WIDTH = 480
HEIGHT = 222
FIRST_VISIBLE_ROW = 49
LAST_VISIBLE_ROW = FIRST_VISIBLE_ROW + HEIGHT - 1


class Window:
    def __init__(self, columns, rows, cycles):
        self.columns = columns
        self.rows = rows
        self.cycles = cycles
        self.pixels = 0
        self.nonzero = 0

    @property
    def width(self):
        return self.columns[1] - self.columns[0] + 1

    @property
    def height(self):
        return self.rows[1] - self.rows[0] + 1

    @property
    def complete(self):
        return self.pixels == self.width * self.height


class Sweep:
    def __init__(self, cycles, windows, nonzero, groups):
        self.cycles = cycles
        self.windows = windows
        self.nonzero = nonzero
        self.groups = groups


class Panel:
    def __init__(self, machine):
        self.machine = machine
        self.groups = []
        self.windows = []
        self.sweeps = []
        self.columns = (0, 0)
        self.rows = (0, 0)
        self.current = None
        self.window = None
        self.position = 0
        self.frame = [0] * (WIDTH * HEIGHT)
        self.painted = bytearray(WIDTH * HEIGHT)
        self.pixel_total = 0
        self.out_of_range = 0
        self.sweep_windows = 0
        self.sweep_nonzero = 0
        self.sweep_next_row = FIRST_VISIBLE_ROW

    def command(self, value):
        self.window = None
        self.current = [value, []]
        self.groups.append(self.current)
        if value == RAMWR:
            self.window = Window(self.columns, self.rows, self.machine.cycles())
            self.windows.append(self.window)
            self.position = 0

    def parameter(self, value):
        if self.current is None:
            return
        self.current[1].append(value)
        command, params = self.current
        if command in (CASET, RASET) and len(params) == 4:
            span = ((params[0] << 8) | params[1], (params[2] << 8) | params[3])
            if command == CASET:
                self.columns = span
            else:
                self.rows = span

    def pixel(self, value):
        window = self.window
        if window is None:
            return
        window.pixels += 1
        window.nonzero += 1 if value else 0
        self.pixel_total += 1
        x = window.columns[0] + self.position % window.width
        y = window.rows[0] + self.position // window.width - FIRST_VISIBLE_ROW
        self.position += 1
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            self.frame[y * WIDTH + x] = value
            self.painted[y * WIDTH + x] = 1
        else:
            self.out_of_range += 1
        if window.complete:
            self.window = None
            self.track_sweep(window)

    def track_sweep(self, window):
        full_width = window.columns == (0, WIDTH - 1)
        if full_width and window.rows[0] == self.sweep_next_row:
            self.sweep_windows += 1
            self.sweep_nonzero += window.nonzero
            self.sweep_next_row = window.rows[1] + 1
        elif full_width and window.rows[0] == FIRST_VISIBLE_ROW:
            self.sweep_windows = 1
            self.sweep_nonzero = window.nonzero
            self.sweep_next_row = window.rows[1] + 1
        else:
            self.sweep_windows = 0
            self.sweep_next_row = FIRST_VISIBLE_ROW
            return
        if self.sweep_next_row == LAST_VISIBLE_ROW + 1:
            self.sweeps.append(Sweep(self.machine.cycles(), self.sweep_windows, self.sweep_nonzero,
                                     len(self.groups)))
            self.sweep_windows = 0
            self.sweep_nonzero = 0
            self.sweep_next_row = FIRST_VISIBLE_ROW

    def init_commands(self):
        commands = []
        for command, params in self.groups:
            commands.append((command, tuple(params)))
            if command == RAMWR:
                break
        return commands

    def painted_pixels(self):
        return sum(self.painted)

    def colors(self):
        return len(set(self.frame))

    def lit_sweeps(self):
        return [sweep for sweep in self.sweeps if sweep.nonzero > 0]
