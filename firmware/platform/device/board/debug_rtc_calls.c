#include <stddef.h>

#include "extension_entry.h"
#include "hal/system.h"

static const ExtensionModuleEntry *extension(void)
{
    return hal_system_extension_code();
}

HalRtcRead hal_system_rtc_read(void)
{
    const ExtensionModuleEntry *code = extension();
    return code != NULL ? code->rtc_read() : (HalRtcRead){.status = HAL_RTC_NO_ACK};
}

HalRtcRead hal_system_rtc_set(uint32_t utc_s)
{
    const ExtensionModuleEntry *code = extension();
    return code != NULL ? code->rtc_set(utc_s) : (HalRtcRead){.status = HAL_RTC_NO_ACK};
}
