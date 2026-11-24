import ctypes
from array import array
from collections import deque
from dataclasses import dataclass
from functools import partial
from gc import get_referents
from types import CellType, FunctionType, MethodType

from unicorn import Uc

from .native_accel import SNAPSHOT_REGION_CB


def _memory_regions(uc):
    return {(first, last + 1): access for first, last, access in uc.mem_regions()}


def _callback_functions(callback):
    """Find the Python callable retained by a ctypes trampoline, including MMIO-only devices."""
    for thunk in (callback._objects or {}).values():
        yield from (ref for ref in get_referents(thunk) if isinstance(ref, FunctionType))


def _native_regions(resolve, pointer):
    regions = []
    resolve(pointer, SNAPSHOT_REGION_CB(lambda address, size, capacity:
                                       regions.append((address, size, capacity))))
    return regions


class _NativeState:
    def __init__(self, engine):
        self.engine = engine
        self.pages = engine.pages.copy()
        self.parts = []
        self.capture(engine.kind, engine.pointer)
        for page in self.pages.values():
            self.capture("generic_page", page)
        for mmc in engine.mmcs.values():
            self.capture("mmc", mmc)
        self.decoder = self.current_decoder()
        if self.decoder is not None:
            self.capture("decoder", self.decoder)

    def current_decoder(self):
        if self.engine.kind == "core1":
            return self.engine.lib.emu_core1_decoder(self.engine.pointer)
        return None

    def capture(self, kind, pointer):
        resolve = getattr(self.engine.lib, f"emu_{kind}_snapshot")
        regions = _native_regions(resolve, pointer)
        data = [ctypes.string_at(address, size) if size else b"" for address, size, _ in regions]
        self.parts.append((resolve, pointer, data))

    def validate(self):
        if not self.pages.items() <= self.engine.pages.items():
            raise ValueError("a native MMIO page mapped at capture has changed")
        if self.decoder is not None and self.current_decoder() != self.decoder:
            raise ValueError("a native decoder registered at capture has changed")

    def restore(self, uc):
        decoder = self.current_decoder()
        if decoder is not None and self.decoder is None:
            self.engine.lib.emu_decoder_destroy(decoder)
            del uc._native_decoder_host
        for resolve, pointer, data in self.parts:
            regions = _native_regions(resolve, pointer)
            for saved, (address, _, capacity) in zip(data, regions, strict=True):
                if len(saved) > capacity:
                    raise ValueError("a native snapshot buffer has lost capacity")
                if saved:
                    ctypes.memmove(address, saved, len(saved))


class _EngineState:
    def __init__(self, uc):
        self.uc = uc
        self.context = uc.context_save()
        self.regions = _memory_regions(uc)
        self.hooks = uc._callbacks.copy()
        self.mmio = uc._mmio_callbacks.copy()
        self.hook_exception = uc._hook_exception
        self.native = None
        mmio = set(self.mmio)
        native = getattr(uc, "_native_engine", None)
        if native is not None:
            self.native = _NativeState(native)
            mmio.update(native.mmio)
            mmio.update(bounds for bounds in self.regions if bounds[0] in native.pages)
        self.ram = {bounds: bytes(uc.mem_read(bounds[0], bounds[1] - bounds[0]))
                    for bounds in self.regions.keys() - mmio}

    def callbacks(self):
        yield from self.hooks.values()
        for pair in self.mmio.values():
            yield from (callback for callback in pair if callback is not None)

    def validate(self):
        uc = self.uc
        if any(uc._callbacks.get(handle) is not cb for handle, cb in self.hooks.items()):
            raise ValueError("a Unicorn hook registered at capture has been removed")
        if any(uc._mmio_callbacks.get(region) is not cb for region, cb in self.mmio.items()):
            raise ValueError("a Unicorn device mapped at capture has been removed")
        if not self.regions.items() <= _memory_regions(uc).items():
            raise ValueError("a Unicorn memory region mapped at capture has changed")
        if self.native is not None:
            self.native.validate()

    def restore(self):
        uc = self.uc
        for handle in uc._callbacks.keys() - self.hooks.keys():
            uc.hook_del(handle)
        for first, end in _memory_regions(uc).keys() - self.regions.keys():
            if self.native is None:
                uc.mem_unmap(first, end - first)
            else:
                self.native.engine.unmap(uc, first, end)
        for (first, _), data in self.ram.items():
            uc.mem_write(first, data)
        if self.native is not None:
            self.native.restore(uc)
        uc.context_restore(self.context)
        uc.ctl_flush_tb()
        uc._hook_exception = self.hook_exception


CONTAINERS = (dict, list, set, deque, bytearray, array)
SCALARS = (int, bool, float, str, bytes, type(None))
HELPER_MODULES = {"echo_image_harness", "usb_audio_host"}


def _restore_contents(target, contents):
    if isinstance(target, (dict, set)):
        target.clear()
        target.update(contents)
    elif isinstance(target, deque):
        target.clear()
        target.extend(contents)
    elif isinstance(target, CellType):
        target.cell_contents = contents
    elif isinstance(target, ctypes.c_uint64):
        target.value = contents
    else:
        target[:] = contents


class _State:
    """Keep the captured object identities and an independent copy of each mutable container."""

    def __init__(self, root):
        self.objects = []
        self.engines = []
        self.seen = set()
        self.visit(root)
        del self.seen

    def visit(self, value):
        if type(value) in SCALARS or id(value) in self.seen:
            return
        self.seen.add(id(value))
        if isinstance(value, CONTAINERS):
            self.container(value)
        elif isinstance(value, (tuple, frozenset)):
            self.visit_items(value)
        elif isinstance(value, Uc):
            engine = _EngineState(value)
            self.engines.append(engine)
            for callback in engine.callbacks():
                self.visit_items(_callback_functions(callback))
        elif isinstance(value, FunctionType):
            self.visit_items(value.__closure__ or ())
            self.visit(value.__defaults__)
            self.visit(value.__kwdefaults__)
        elif isinstance(value, MethodType):
            self.visit(value.__self__)
        elif isinstance(value, partial):
            self.visit_items((value.func, value.args, value.keywords))
        else:
            self.visit_object(value)

    def visit_items(self, items):
        for item in items:
            self.visit(item)

    def container(self, value):
        if isinstance(value, dict):
            self.objects.append((value, value.copy()))
            self.visit_items(value.keys())
            self.visit_items(value.values())
        elif isinstance(value, bytearray):
            self.objects.append((value, bytes(value)))
        else:
            self.objects.append((value, value[:]
                                 if isinstance(value, array) else value.copy()))
            if not isinstance(value, array):
                self.visit_items(value)

    def visit_object(self, value):
        module = type(value).__module__
        if isinstance(value, CellType):
            try:
                contents = value.cell_contents
            except ValueError:
                return
            self.objects.append((value, contents))
            self.visit(contents)
        elif isinstance(value, ctypes.c_uint64):
            self.objects.append((value, value.value))
        elif module in HELPER_MODULES or (
                module.startswith("echo_emulator.") and module != __name__):
            self.visit(vars(value))

    def restore(self):
        for engine in self.engines:
            engine.validate()
        for engine in self.engines:
            engine.restore()
        for target, contents in self.objects:
            _restore_contents(target, contents)


@dataclass(frozen=True, repr=False)
class SessionSnapshot:
    _session: object
    _state: _State


@dataclass(frozen=True, repr=False)
class RigSnapshot:
    _rig: object
    _state: _State


def capture_session_snapshot(session):
    """Capture a paused session for repeated restores into that same session.

    Unicorn memory mappings and hooks present at capture must remain installed.
    Restore removes mappings and hooks added by the variation.
    """
    return SessionSnapshot(session, _State(session))


def restore_session_snapshot(session, snapshot):
    if snapshot._session is not session:
        raise ValueError("a session snapshot belongs to its original session")
    snapshot._state.restore()


def capture_rig_snapshot(rig):
    """Capture a paused rig and its audio path, with the same scope as a session snapshot."""
    return RigSnapshot(rig, _State(rig))


def restore_rig_snapshot(rig, snapshot):
    if snapshot._rig is not rig:
        raise ValueError("a rig snapshot belongs to its original rig")
    snapshot._state.restore()
