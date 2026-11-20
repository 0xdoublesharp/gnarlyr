#include <stdbool.h>

#include "board.h"
#include "debug_probe.h"
#include "hal/system.h"
#include "usb.h"
#include "usb_registers.h"
#include "usb_session.h"

void hal_system_keep_usb_on(bool kept_on)
{
    board_debug_keep_usb_on(kept_on);
    if (usb_session_shared()->reconnect.in_session || board_usb_listening()) {
        return;
    }
    bool powered = board_debug_usb_is_powered();
    if (kept_on && !powered) {
        usb_power_up();
        set_bits(DCTL, DCTL_SOFT_DISCONNECT);
    } else if (!kept_on && powered) {
        usb_power_down();
    }
}
