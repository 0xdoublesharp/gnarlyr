#include "device/usb_activity.h"

enum {
    SECTORS_PER_KIB = 1024 / MSC_SECTOR_SIZE
};

void usb_activity_start(UsbActivity *activity, uint32_t now_ms)
{
    *activity = (UsbActivity){.read_ms = now_ms, .written_ms = now_ms};
}

void usb_activity_follow(UsbActivity *activity, const MscStats *stats, uint32_t now_ms)
{
    if (stats->sectors_read != activity->read) {
        activity->read = stats->sectors_read;
        activity->read_ms = now_ms;
        activity->has_read = true;
    }
    if (stats->sectors_written != activity->written || stats->flushes != activity->flushes) {
        activity->written = stats->sectors_written;
        activity->flushes = stats->flushes;
        activity->written_ms = now_ms;
        activity->has_written = true;
    }
}

static bool recent(bool happened, uint32_t at_ms, uint32_t now_ms, uint32_t span_ms)
{
    return happened && now_ms - at_ms < span_ms;
}

UsbActivityKind usb_activity_now(const UsbActivity *activity, uint32_t now_ms)
{
    if (recent(activity->has_written, activity->written_ms, now_ms, USB_ACTIVITY_SHOW_MS)) {
        return USB_ACTIVITY_WRITING;
    }
    if (recent(activity->has_read, activity->read_ms, now_ms, USB_ACTIVITY_SHOW_MS)) {
        return USB_ACTIVITY_READING;
    }
    return USB_ACTIVITY_IDLE;
}

bool usb_activity_quiet(const UsbActivity *activity, uint32_t now_ms)
{
    return !recent(activity->has_written, activity->written_ms, now_ms, USB_ACTIVITY_QUIET_MS);
}

uint32_t usb_activity_written_kib(const UsbActivity *activity)
{
    return activity->written / SECTORS_PER_KIB;
}
