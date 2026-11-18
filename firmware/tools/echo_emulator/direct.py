import ctypes

from unicorn import UcError
from unicorn.unicorn_py3 import unicorn as binding


class RegisterReader:
    """Reads plain core registers through `uc_reg_read` with one reused buffer.

    The binding picks a register class and builds a fresh buffer on every read; the run loop
    reads the program counter and PRIMASK on every slice. Other registers go to the binding.
    """

    def __init__(self, uc):
        self.uc = uc
        self.handle = uc._uch
        self.value = ctypes.c_uint64()
        self.pointer = ctypes.byref(self.value)
        self.plain = {}

    def __call__(self, register):
        plain = self.plain.get(register)
        if plain is None:
            plain = self.plain[register] = self.uc._select_reg_class(register) is ctypes.c_uint64
        if not plain:
            return self.uc.reg_read(register)
        self.value.value = 0
        status = binding.uclib.uc_reg_read(self.handle, register, self.pointer)
        if status != binding.uc.UC_ERR_OK:
            raise UcError(status, register)
        return self.value.value


def callback_failed(uc, error):
    if uc._hook_exception is None:
        uc._hook_exception = error
    uc.emu_stop()


def map_device(uc, base, size, device):
    """Map `device` straight onto the library's MMIO callbacks.

    The binding's own `mmio_map` passes every access through two wrapper frames before the
    device sees it; registers polled in tight loops make that the largest Python cost. Errors
    are kept for `emu_start` to raise, as the binding does.
    """

    def read(handle, offset, width, user):
        try:
            return device.read(offset, width)
        except BaseException as error:
            callback_failed(uc, error)
            return 0

    def write(handle, offset, width, value, user):
        try:
            device.write(offset, width, value)
        except BaseException as error:
            callback_failed(uc, error)

    callbacks = (binding.MMIO_READ_CFUNC(read), binding.MMIO_WRITE_CFUNC(write))
    status = binding.uclib.uc_mmio_map(uc._uch, base, size, callbacks[0], 0, callbacks[1], 0)
    if status != binding.uc.UC_ERR_OK:
        raise UcError(status)
    uc._mmio_callbacks[(base, base + size)] = callbacks


CODE_HOOKS = (binding.uc.UC_HOOK_CODE, binding.uc.UC_HOOK_BLOCK)
ACCESS_HOOKS = binding.uc.UC_HOOK_MEM_READ | binding.uc.UC_HOOK_MEM_WRITE | binding.uc.UC_HOOK_MEM_FETCH


def code_function(uc, callback, user_data):
    def code(handle, address, size, key):
        try:
            callback(uc, address, size, user_data)
        except BaseException as error:
            callback_failed(uc, error)

    return binding.HOOK_CODE_CFUNC(code)


def access_function(uc, callback, user_data):
    def access(handle, kind, address, size, value, key):
        try:
            callback(uc, kind, address, size, value, user_data)
        except BaseException as error:
            callback_failed(uc, error)

    return binding.HOOK_MEM_ACCESS_CFUNC(access)


def hook_add(uc, kind, callback, user_data=None, begin=1, end=0):
    """`Uc.hook_add` for code, block and memory access hooks, one wrapper frame shorter.

    The callback gets the binding's arguments, and the handle works with `Uc.hook_del`. Other
    hook kinds go to the binding.
    """
    if kind in CODE_HOOKS:
        function = code_function(uc, callback, user_data)
    elif kind and not kind & ~ACCESS_HOOKS:
        function = access_function(uc, callback, user_data)
    else:
        return uc.hook_add(kind, callback, user_data, begin, end)
    handle = binding.uc_hook_h()
    status = binding.uclib.uc_hook_add(uc._uch, ctypes.byref(handle), kind, function,
                                       ctypes.c_void_p(0), ctypes.c_uint64(begin),
                                       ctypes.c_uint64(end))
    if status != binding.uc.UC_ERR_OK:
        raise UcError(status)
    uc._callbacks[handle.value] = function
    return handle.value
