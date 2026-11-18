from unicorn import UcError
from unicorn.unicorn_py3 import unicorn as binding


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
