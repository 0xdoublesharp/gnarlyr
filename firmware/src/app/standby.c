#include "standby.h"
#include "app/app.h"

#include "core/seam.h"
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/clock.h"
#include "hal/power.h"

CORE_SEAM void standby_follow(Standby *standby, PowerPolicy *power, const Settings *settings)
{
    static const uint32_t DURATIONS[] = {1800000, 7200000, 28800000};
    power->standby_enabled = settings->standby_enabled;
    uint8_t option = settings->standby_power_off_option;
    power->standby_duration_ms = DURATIONS[option < 3 ? option : 1];
    if (standby->active == power->standby_active) {
        return;
    }
    standby->active = power->standby_active;
    HalBattery battery = hal_power_battery();
    if (standby->active) {
        standby->entries++;
        standby->entry_ms = hal_clock_milliseconds();
        standby->entry_mv = battery.known ? battery.millivolts : 0;
        standby->bluetooth_enabled = hal_bluetooth_status()->state != HAL_BLUETOOTH_OFF;
        app_release_audio();
        hal_audio_release();
        hal_bluetooth_set_power(false);
    } else {
        standby->wakes++;
        standby->wake_ms = hal_clock_milliseconds();
        standby->wake_mv = battery.known ? battery.millivolts : 0;
        standby->waking = true;
        hal_bluetooth_set_power(standby->bluetooth_enabled);
    }
}

void standby_frame(Standby *standby)
{
    if (standby->waking) {
        standby->latency_ms = hal_clock_milliseconds() - standby->wake_ms;
        standby->waking = false;
    }
}
