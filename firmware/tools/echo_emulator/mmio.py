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
