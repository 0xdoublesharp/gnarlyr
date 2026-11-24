import ctypes
import hashlib
import os
import platform
import subprocess
import tempfile
import weakref
from pathlib import Path

import unicorn

EXIT_TIME_LIMIT = 1
EXIT_SCHEDULED = 2
EXIT_STEP_LIMIT = 3
EXIT_INSN_LIMIT = 4
EXIT_HALTED = 5
EXIT_WATCHDOG = 6
EXIT_RESET = 7
EXIT_UC_ERROR = 8

GPIO_WRITE_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)
GRF_WRITE_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32)
MMC_READ_CB = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_uint32, ctypes.c_uint32)
MMC_WRITE_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint64)
SNAPSHOT_REGION_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t)


def _close_engine(close, args, kwargs, resources):
    close(*args, **kwargs)


class NativeEngine:
    def __init__(self, uc, lib, kind, pointer, shared_owner=None):
        self.lib = lib
        self.kind = kind
        self.pointer = pointer
        self.shared_owner = shared_owner
        self.mmio = set()
        self.mmcs = {}
        self.pages = {}
        _, close, args, kwargs = uc._Uc__finalizer.detach()
        uc._Uc__finalizer = weakref.finalize(uc, _close_engine, close, args, kwargs, self)
        uc._native_engine = self

    def __del__(self):
        for page in self.pages.values():
            self.lib.emu_generic_page_destroy(page)
        for mmc in self.mmcs.values():
            self.lib.emu_mmc_destroy(mmc)
        getattr(self.lib, f"emu_{self.kind}_destroy")(self.pointer)

    def unmap(self, uc, first, end):
        uc.mem_unmap(first, end - first)
        page = self.pages.pop(first, None)
        if page is not None:
            self.lib.emu_generic_page_destroy(page)


class EmuWindow(ctypes.Structure):
    _fields_ = [
        ("c0", ctypes.c_int32),
        ("c1", ctypes.c_int32),
        ("r0", ctypes.c_int32),
        ("r1", ctypes.c_int32),
        ("cycles", ctypes.c_uint64),
        ("pixels", ctypes.c_int64),
        ("nonzero", ctypes.c_int64),
    ]


class EmuSweep(ctypes.Structure):
    _fields_ = [
        ("cycles", ctypes.c_uint64),
        ("windows", ctypes.c_int32),
        ("nonzero", ctypes.c_int64),
        ("groups", ctypes.c_int32),
    ]


class EmuCore0Header(ctypes.Structure):
    _fields_ = [
        ("base_cycles", ctypes.c_uint64),
        ("jump_cycles", ctypes.c_uint64),
        ("core_hz", ctypes.c_uint64),
        ("pclk_hz", ctypes.c_uint64),
        ("rate_mark_cycles", ctypes.c_uint64),
        ("rate_mark_us", ctypes.c_uint64),
        ("systick_base_cycle", ctypes.c_uint64),
        ("systick_delivered_wraps", ctypes.c_uint64),
        ("systick_reload", ctypes.c_uint32),
        ("systick_vtor", ctypes.c_uint32),
        ("systick_enabled", ctypes.c_uint8),
        ("systick_tick_interrupt", ctypes.c_uint8),
        ("exception_depth", ctypes.c_uint8),
        ("pending_return", ctypes.c_uint8),
        ("halting", ctypes.c_uint8),
        ("budget_hit", ctypes.c_uint8),
        ("rest_pending", ctypes.c_uint8),
        ("python_exit", ctypes.c_uint8),
        ("reset_requested_flag", ctypes.c_uint8),
        ("card_present", ctypes.c_uint8),
        ("balanced_plugged", ctypes.c_uint8),
        ("vbus", ctypes.c_uint8),
        ("cable_bit_dead", ctypes.c_uint8),
        ("wdog_running", ctypes.c_uint8),
        ("pad0", ctypes.c_uint8 * 2),
        ("wdog_top", ctypes.c_uint32),
        ("adc_key_value", ctypes.c_uint32),
        ("adc_battery_value", ctypes.c_uint32),
        ("parked", ctypes.c_uint32),
        ("interrupted_parked", ctypes.c_uint32),
        ("entered", ctypes.c_uint32),
        ("resumed", ctypes.c_uint32),
        ("last_err", ctypes.c_uint32),
        ("has_sched", ctypes.c_uint32),
        ("wdog_last_kick", ctypes.c_uint64),
        ("wdog_kicks", ctypes.c_uint64),
        ("reset_cycle", ctypes.c_uint64),
        ("power_key_pressed_at_ms", ctypes.c_int64),
        ("power_key_released_at_ms", ctypes.c_int64),
        ("next_sched_ms", ctypes.c_int64),
        ("pending_insns", ctypes.c_uint64),
        ("slice_insns", ctypes.c_uint64),
    ]


class EmuDecoderHeader(ctypes.Structure):
    _fields_ = [
        ("instructions", ctypes.c_uint64),
        ("wakes", ctypes.c_uint64),
        ("busy_us", ctypes.c_double),
        ("time_us", ctypes.c_double),
        ("period_start", ctypes.c_double),
        ("next_wake_us", ctypes.c_double),
        ("mhz", ctypes.c_uint32),
        ("reload", ctypes.c_uint32),
        ("wfi", ctypes.c_uint32),
        ("counter", ctypes.c_uint32),
        ("ring_bytes", ctypes.c_uint32),
        ("in_units", ctypes.c_uint8),
        ("resting", ctypes.c_uint8),
        ("awake", ctypes.c_uint8),
        ("has_copied", ctypes.c_uint8),
        ("copied_gen", ctypes.c_uint32),
        ("copied_written", ctypes.c_uint32),
        ("clock_count", ctypes.c_uint32),
        ("clock_mhz", ctypes.c_uint32 * 16),
        ("clock_us", ctypes.c_double * 16),
    ]


_CACHED = False
_LIB = None


def _unicorn_paths():
    root = Path(unicorn.__file__).resolve().parent
    inc = root / "include"
    lib_dir = root / "lib"
    if not (inc / "unicorn" / "unicorn.h").is_file() or not lib_dir.is_dir():
        return None, None, None
    system = platform.system()
    candidates = ("libunicorn.2.dylib", "libunicorn.dylib") if system == "Darwin" else (
        "libunicorn.so.2", "libunicorn.so")
    for name in candidates:
        candidate = lib_dir / name
        if candidate.is_file():
            return inc, lib_dir, candidate
    return None, None, None


def _cache_dir():
    firmware_dir = Path(__file__).resolve().parents[2]
    build_dir = firmware_dir / "build"
    if build_dir.is_dir():
        path = build_dir / "native_cache"
    else:
        path = Path(tempfile.gettempdir()) / "echo_emu_native_cache"
    path.mkdir(parents=True, exist_ok=True)
    return path


def _configure_signatures(lib):
    vp = ctypes.c_void_p
    u8 = ctypes.c_uint8
    u16 = ctypes.c_uint16
    u32 = ctypes.c_uint32
    u64 = ctypes.c_uint64
    i32 = ctypes.c_int32
    i64 = ctypes.c_int64
    dbl = ctypes.c_double

    lib.emu_core0_create.argtypes = [vp, GPIO_WRITE_CB, GRF_WRITE_CB]
    lib.emu_core0_create.restype = vp
    lib.emu_core0_destroy.argtypes = [vp]
    lib.emu_core0_destroy.restype = None
    lib.emu_core0_header.argtypes = [vp]
    lib.emu_core0_header.restype = ctypes.POINTER(EmuCore0Header)
    lib.emu_core0_panel.argtypes = [vp]
    lib.emu_core0_panel.restype = vp
    lib.emu_core0_core1_ram.argtypes = [vp]
    lib.emu_core0_core1_ram.restype = vp
    lib.emu_core0_ram.argtypes = [vp]
    lib.emu_core0_ram.restype = vp
    lib.emu_core0_memcmp.argtypes = [vp, u32, ctypes.c_char_p, u32]
    lib.emu_core0_memcmp.restype = ctypes.c_int
    lib.emu_core0_systick_restart.argtypes = [vp]
    lib.emu_core0_systick_restart.restype = None
    lib.emu_core0_systick_acknowledge.argtypes = [vp]
    lib.emu_core0_systick_acknowledge.restype = None
    lib.emu_core0_recompute_budget.argtypes = [vp]
    lib.emu_core0_recompute_budget.restype = None
    lib.emu_core0_clear_block_cache.argtypes = [vp]
    lib.emu_core0_clear_block_cache.restype = None
    lib.emu_core0_run.argtypes = [vp, i64, ctypes.c_int, u64, u64]
    lib.emu_core0_run.restype = ctypes.c_int
    lib.emu_core0_step_once.argtypes = [vp, u64]
    lib.emu_core0_step_once.restype = ctypes.c_int

    lib.emu_generic_page_map.argtypes = [vp, u32]
    lib.emu_generic_page_map.restype = vp
    lib.emu_generic_page_destroy.argtypes = [vp]
    lib.emu_generic_page_destroy.restype = None
    lib.emu_generic_page_drain_touched.argtypes = [vp, ctypes.POINTER(u32), u32]
    lib.emu_generic_page_drain_touched.restype = u32

    lib.emu_mmc_map.argtypes = [vp, u32, MMC_READ_CB, MMC_WRITE_CB]
    lib.emu_mmc_map.restype = vp
    lib.emu_mmc_destroy.argtypes = [vp]
    lib.emu_mmc_destroy.restype = None
    lib.emu_mmc_sync.argtypes = [vp, u64, u32]
    lib.emu_mmc_sync.restype = None
    lib.emu_mmc_clear_fifo.argtypes = [vp]
    lib.emu_mmc_clear_fifo.restype = None
    lib.emu_mmc_load_fifo.argtypes = [vp, ctypes.POINTER(u32), u32]
    lib.emu_mmc_load_fifo.restype = None

    lib.emu_panel_command.argtypes = [vp, u8]
    lib.emu_panel_command.restype = None
    lib.emu_panel_parameter.argtypes = [vp, u8]
    lib.emu_panel_parameter.restype = None
    lib.emu_panel_pixel.argtypes = [vp, u16]
    lib.emu_panel_pixel.restype = None
    lib.emu_panel_pixels.argtypes = [vp, ctypes.POINTER(u16), u32]
    lib.emu_panel_pixels.restype = None
    lib.emu_panel_frame_ptr.argtypes = [vp]
    lib.emu_panel_frame_ptr.restype = ctypes.POINTER(u16)
    lib.emu_panel_painted_ptr.argtypes = [vp]
    lib.emu_panel_painted_ptr.restype = ctypes.POINTER(u8)
    lib.emu_panel_pixel_total.argtypes = [vp]
    lib.emu_panel_pixel_total.restype = u64
    lib.emu_panel_out_of_range.argtypes = [vp]
    lib.emu_panel_out_of_range.restype = u64
    lib.emu_panel_painted_count.argtypes = [vp]
    lib.emu_panel_painted_count.restype = u32
    lib.emu_panel_color_count.argtypes = [vp]
    lib.emu_panel_color_count.restype = u32
    lib.emu_panel_group_count.argtypes = [vp]
    lib.emu_panel_group_count.restype = u32
    lib.emu_panel_param_len.argtypes = [vp]
    lib.emu_panel_param_len.restype = u32
    lib.emu_panel_window_count.argtypes = [vp]
    lib.emu_panel_window_count.restype = u32
    lib.emu_panel_sweep_count.argtypes = [vp]
    lib.emu_panel_sweep_count.restype = u32
    lib.emu_panel_lit_sweep_count.argtypes = [vp]
    lib.emu_panel_lit_sweep_count.restype = u32
    lib.emu_panel_get_group.argtypes = [vp, u32, ctypes.POINTER(u8), ctypes.POINTER(u32),
                                        ctypes.POINTER(u32)]
    lib.emu_panel_get_group.restype = None
    lib.emu_panel_params_ptr.argtypes = [vp]
    lib.emu_panel_params_ptr.restype = ctypes.POINTER(u8)
    lib.emu_panel_get_window.argtypes = [vp, u32, ctypes.POINTER(EmuWindow)]
    lib.emu_panel_get_window.restype = None
    lib.emu_panel_get_sweep.argtypes = [vp, u32, ctypes.POINTER(EmuSweep)]
    lib.emu_panel_get_sweep.restype = None
    lib.emu_panel_get_bounds.argtypes = [vp, ctypes.POINTER(i32), ctypes.POINTER(i32),
                                         ctypes.POINTER(i32), ctypes.POINTER(i32)]
    lib.emu_panel_get_bounds.restype = None
    lib.emu_panel_dmac_pushed.argtypes = [vp]
    lib.emu_panel_dmac_pushed.restype = u64
    lib.emu_panel_vop_pixel_mode.argtypes = [vp]
    lib.emu_panel_vop_pixel_mode.restype = u32

    lib.emu_core1_create.argtypes = [vp, vp]
    lib.emu_core1_create.restype = vp
    lib.emu_core1_destroy.argtypes = [vp]
    lib.emu_core1_destroy.restype = None
    lib.emu_core1_decoder.argtypes = [vp]
    lib.emu_core1_decoder.restype = vp
    lib.emu_core1_clear_cache.argtypes = [vp]
    lib.emu_core1_clear_cache.restype = None
    lib.emu_core1_read_u32.argtypes = [vp, u32]
    lib.emu_core1_read_u32.restype = u32
    lib.emu_core1_write_u32.argtypes = [vp, u32, u32]
    lib.emu_core1_write_u32.restype = None
    lib.emu_core1_run_budget.argtypes = [vp, u32, u64, ctypes.POINTER(u64)]
    lib.emu_core1_run_budget.restype = u32
    lib.emu_core1_clock.argtypes = [vp]
    lib.emu_core1_clock.restype = u32
    lib.emu_core1_clock_write_count.argtypes = [vp]
    lib.emu_core1_clock_write_count.restype = u32
    lib.emu_core1_clock_writes_ptr.argtypes = [vp]
    lib.emu_core1_clock_writes_ptr.restype = ctypes.POINTER(u32)
    lib.emu_core1_clear_clock_writes.argtypes = [vp]
    lib.emu_core1_clear_clock_writes.restype = None
    lib.emu_core1_shared_ptr.argtypes = [vp]
    lib.emu_core1_shared_ptr.restype = vp

    lib.emu_decoder_attach.argtypes = [vp, vp, vp, u32, u32, u8, u32]
    lib.emu_decoder_attach.restype = vp
    lib.emu_decoder_destroy.argtypes = [vp]
    lib.emu_decoder_destroy.restype = None
    lib.emu_decoder_header.argtypes = [vp]
    lib.emu_decoder_header.restype = ctypes.POINTER(EmuDecoderHeader)
    lib.emu_decoder_deliver.argtypes = [vp]
    lib.emu_decoder_deliver.restype = None
    lib.emu_decoder_wake.argtypes = [vp]
    lib.emu_decoder_wake.restype = None
    lib.emu_decoder_run_until.argtypes = [vp, dbl]
    lib.emu_decoder_run_until.restype = ctypes.c_int
    lib.emu_decoder_publish.argtypes = [vp]
    lib.emu_decoder_publish.restype = None
    lib.emu_decoder_copy_input.argtypes = [vp]
    lib.emu_decoder_copy_input.restype = None
    lib.emu_decoder_advance_to.argtypes = [vp, dbl]
    lib.emu_decoder_advance_to.restype = ctypes.c_int
    lib.emu_decoder_step.argtypes = [vp, dbl]
    lib.emu_decoder_step.restype = ctypes.c_int

    for kind in ("core0", "core1", "decoder", "generic_page", "mmc"):
        snapshot = getattr(lib, f"emu_{kind}_snapshot")
        snapshot.argtypes = [vp, SNAPSHOT_REGION_CB]
        snapshot.restype = None


def load_native():
    global _CACHED, _LIB
    if os.environ.get("ECHO_EMU_NATIVE", "1") == "0":
        return None
    if _CACHED:
        return _LIB
    _CACHED = True
    src = Path(__file__).resolve().parent / "native" / "echo_emu_native.c"
    if not src.is_file():
        return None
    inc, lib_dir, uc_lib = _unicorn_paths()
    if inc is None:
        return None
    digest = hashlib.sha256(src.read_bytes() + str(uc_lib).encode("utf-8")).hexdigest()[:16]
    suffix = ".dylib" if platform.system() == "Darwin" else ".so"
    target = _cache_dir() / f"libecho_emu_native_{digest}{suffix}"
    if not target.is_file():
        tmp = target.with_name(f"{target.stem}.{os.getpid()}{suffix}")
        cmd = [
            "cc", "-O3", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
            f"-I{inc}", str(src), str(uc_lib),
            f"-Wl,-rpath,{lib_dir}", "-o", str(tmp),
        ]
        try:
            subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.replace(tmp, target)
        except (OSError, subprocess.CalledProcessError):
            try:
                tmp.unlink(missing_ok=True)
            except OSError:
                pass
            return None
    try:
        lib = ctypes.CDLL(str(target))
        _configure_signatures(lib)
        _LIB = lib
    except OSError:
        return None
    return _LIB
