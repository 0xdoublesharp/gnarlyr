import array
import ctypes

from . import native_accel

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
        self._native_lib = None
        self._native_ptr = None
        self._groups = []
        self._windows = []
        self._sweeps = []
        self._columns = (0, 0)
        self._rows = (0, 0)
        self._current = None
        self.window = None
        self.position = 0
        self._frame = [0] * (WIDTH * HEIGHT)
        self._painted = bytearray(WIDTH * HEIGHT)
        self._pixel_total = 0
        self._out_of_range = 0
        self.sweep_windows = 0
        self.sweep_nonzero = 0
        self.sweep_next_row = FIRST_VISIBLE_ROW
        self._synced_param_len = 0
        self._frame_synced_total = -1
        self._painted_synced_total = -1
        self._window_buf = native_accel.EmuWindow()
        self._sweep_buf = native_accel.EmuSweep()

    def attach_native(self, lib, ptr):
        self._native_lib = lib
        self._native_ptr = ptr
        self._frame_synced_total = -1
        self._painted_synced_total = -1

    @property
    def current(self):
        if self._native_ptr is None:
            return self._current
        groups = self.groups
        return groups[-1] if groups else None

    @property
    def groups(self):
        if self._native_ptr is None:
            return self._groups
        lib, ptr = self._native_lib, self._native_ptr
        count = lib.emu_panel_group_count(ptr)
        param_len = lib.emu_panel_param_len(ptr)
        if count == len(self._groups) and param_len == self._synced_param_len:
            return self._groups
        params_ptr = lib.emu_panel_params_ptr(ptr)
        cmd = ctypes.c_uint8()
        offset = ctypes.c_uint32()
        plen = ctypes.c_uint32()
        if self._groups and param_len > self._synced_param_len:
            last_idx = len(self._groups) - 1
            lib.emu_panel_get_group(ptr, last_idx, ctypes.byref(cmd), ctypes.byref(offset),
                                    ctypes.byref(plen))
            existing = len(self._groups[last_idx][1])
            if plen.value > existing:
                raw = ctypes.string_at(
                    ctypes.addressof(params_ptr.contents) + offset.value + existing,
                    plen.value - existing)
                self._groups[last_idx][1].extend(raw)
        for index in range(len(self._groups), count):
            lib.emu_panel_get_group(ptr, index, ctypes.byref(cmd), ctypes.byref(offset),
                                    ctypes.byref(plen))
            if plen.value:
                raw = ctypes.string_at(ctypes.addressof(params_ptr.contents) + offset.value,
                                       plen.value)
                plist = list(raw)
            else:
                plist = []
            self._groups.append([cmd.value, plist])
        self._synced_param_len = param_len
        return self._groups

    @property
    def windows(self):
        if self._native_ptr is None:
            return self._windows
        lib, ptr = self._native_lib, self._native_ptr
        count = lib.emu_panel_window_count(ptr)
        buf = self._window_buf
        if self._windows:
            last = len(self._windows) - 1
            lib.emu_panel_get_window(ptr, last, ctypes.byref(buf))
            self._windows[last].pixels = buf.pixels
            self._windows[last].nonzero = buf.nonzero
        for index in range(len(self._windows), count):
            lib.emu_panel_get_window(ptr, index, ctypes.byref(buf))
            win = Window((buf.c0, buf.c1), (buf.r0, buf.r1), buf.cycles)
            win.pixels = buf.pixels
            win.nonzero = buf.nonzero
            self._windows.append(win)
        return self._windows

    @property
    def sweeps(self):
        if self._native_ptr is None:
            return self._sweeps
        lib, ptr = self._native_lib, self._native_ptr
        count = lib.emu_panel_sweep_count(ptr)
        if count == len(self._sweeps):
            return self._sweeps
        buf = self._sweep_buf
        for index in range(len(self._sweeps), count):
            lib.emu_panel_get_sweep(ptr, index, ctypes.byref(buf))
            self._sweeps.append(Sweep(buf.cycles, buf.windows, buf.nonzero, buf.groups))
        return self._sweeps

    @property
    def frame(self):
        if self._native_ptr is None:
            return self._frame
        lib, ptr = self._native_lib, self._native_ptr
        total = lib.emu_panel_pixel_total(ptr)
        if total != self._frame_synced_total:
            raw = ctypes.string_at(lib.emu_panel_frame_ptr(ptr), WIDTH * HEIGHT * 2)
            values = array.array("H")
            values.frombytes(raw)
            self._frame = values.tolist()
            self._frame_synced_total = total
        return self._frame

    @property
    def painted(self):
        if self._native_ptr is None:
            return self._painted
        lib, ptr = self._native_lib, self._native_ptr
        total = lib.emu_panel_pixel_total(ptr)
        if total != self._painted_synced_total:
            raw = ctypes.string_at(lib.emu_panel_painted_ptr(ptr), WIDTH * HEIGHT)
            self._painted = bytearray(raw)
            self._painted_synced_total = total
        return self._painted

    @property
    def pixel_total(self):
        if self._native_ptr is None:
            return self._pixel_total
        return self._native_lib.emu_panel_pixel_total(self._native_ptr)

    @property
    def out_of_range(self):
        if self._native_ptr is None:
            return self._out_of_range
        return self._native_lib.emu_panel_out_of_range(self._native_ptr)

    @property
    def columns(self):
        if self._native_ptr is None:
            return self._columns
        c0, c1, r0, r1 = ctypes.c_int32(), ctypes.c_int32(), ctypes.c_int32(), ctypes.c_int32()
        self._native_lib.emu_panel_get_bounds(self._native_ptr, ctypes.byref(c0), ctypes.byref(c1),
                                              ctypes.byref(r0), ctypes.byref(r1))
        return (c0.value, c1.value)

    @property
    def rows(self):
        if self._native_ptr is None:
            return self._rows
        c0, c1, r0, r1 = ctypes.c_int32(), ctypes.c_int32(), ctypes.c_int32(), ctypes.c_int32()
        self._native_lib.emu_panel_get_bounds(self._native_ptr, ctypes.byref(c0), ctypes.byref(c1),
                                              ctypes.byref(r0), ctypes.byref(r1))
        return (r0.value, r1.value)

    def command(self, value):
        if self._native_ptr is not None:
            self._native_lib.emu_panel_command(self._native_ptr, value & 0xFF)
            return
        self.window = None
        self._current = [value, []]
        self._groups.append(self._current)
        if value == RAMWR:
            self.window = Window(self._columns, self._rows, self.machine.cycles())
            self._windows.append(self.window)
            self.position = 0

    def parameter(self, value):
        if self._native_ptr is not None:
            self._native_lib.emu_panel_parameter(self._native_ptr, value & 0xFF)
            return
        if self.current is None:
            return
        self.current[1].append(value)
        command, params = self.current
        if command in (CASET, RASET) and len(params) == 4:
            span = ((params[0] << 8) | params[1], (params[2] << 8) | params[3])
            if command == CASET:
                self._columns = span
            else:
                self._rows = span

    def pixels(self, values):
        if self._native_ptr is not None:
            arr = (ctypes.c_uint16 * len(values))(*values)
            self._native_lib.emu_panel_pixels(self._native_ptr, arr, len(values))
            return
        window = self.window
        if window is None:
            return
        if window.width <= 0 or window.height <= 0:
            for value in values:
                self.pixel(value)
            return
        count = min(len(values), window.width * window.height - window.pixels)
        accepted = values[:count]
        window.pixels += count
        window.nonzero += count - accepted.count(0)
        self._pixel_total += count
        self.place_span(window, accepted)
        if window.complete:
            self.window = None
            self.track_sweep(window)

    def place_span(self, window, values):
        width, done = window.width, 0
        while done < len(values):
            row, column = divmod(self.position, width)
            length = min(len(values) - done, width - column)
            self.place_row(window.rows[0] + row - FIRST_VISIBLE_ROW,
                           window.columns[0] + column, values[done:done + length])
            self.position += length
            done += length

    def place_row(self, y, x, values):
        first, last = max(x, 0), min(x + len(values), WIDTH)
        if not 0 <= y < HEIGHT or first >= last:
            self._out_of_range += len(values)
            return
        start = y * WIDTH
        self._frame[start + first:start + last] = values[first - x:last - x]
        self._painted[start + first:start + last] = b"\x01" * (last - first)
        self._out_of_range += len(values) - (last - first)

    def pixel(self, value):
        if self._native_ptr is not None:
            self._native_lib.emu_panel_pixel(self._native_ptr, value & 0xFFFF)
            return
        window = self.window
        if window is None:
            return
        window.pixels += 1
        window.nonzero += 1 if value else 0
        self._pixel_total += 1
        x = window.columns[0] + self.position % window.width
        y = window.rows[0] + self.position // window.width - FIRST_VISIBLE_ROW
        self.position += 1
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            self._frame[y * WIDTH + x] = value
            self._painted[y * WIDTH + x] = 1
        else:
            self._out_of_range += 1
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
            self._sweeps.append(Sweep(self.machine.cycles(), self.sweep_windows, self.sweep_nonzero,
                                      len(self._groups)))
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
        if self._native_ptr is not None:
            return self._native_lib.emu_panel_painted_count(self._native_ptr)
        return sum(self._painted)

    def colors(self):
        if self._native_ptr is not None:
            return self._native_lib.emu_panel_color_count(self._native_ptr)
        return len(set(self._frame))

    def lit_sweeps(self):
        if self._native_ptr is not None:
            if self._native_lib.emu_panel_lit_sweep_count(self._native_ptr) == 0:
                return []
        return [sweep for sweep in self.sweeps if sweep.nonzero > 0]
