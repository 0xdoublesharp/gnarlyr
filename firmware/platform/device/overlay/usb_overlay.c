#include "debug_probe.h"
#include "overlay.h"
#include "screens/charge_screen.h"
#include "usb_session.h"

#ifdef ECHO_DEBUG_LOOP
const UsbOverlayEntry USB_OVERLAY_ENTRY = {USB_OVERLAY_MAGIC, usb_storage_session, usb_dac_session,
                                           usb_screen_draw, debug_report_write};
#else
const UsbOverlayEntry USB_OVERLAY_ENTRY = {USB_OVERLAY_MAGIC, usb_storage_session, usb_dac_session,
                                           usb_screen_draw};
#endif
