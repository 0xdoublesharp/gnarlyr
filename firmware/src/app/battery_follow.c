#include "app_screens.h"

#include "hal/system.h"

static BatteryLoadClass load_class(const AppScreensContext *context, bool display_on)
{
    if (display_on) {
        return BATTERY_LOAD_SCREEN;
    }
    if (context->player->state != PLAYER_PLAYING) {
        return BATTERY_LOAD_IDLE;
    }
    uint8_t volume = player_volume(context->player, PLAYER_OUTPUT_WIRED);
    bool high_gain = context->settings->settings.gain == GAIN_HIGH;
    bool heavy = hal_system_clock_mhz() > 96 || context->bluetooth->connected || volume >= 48 ||
                 (high_gain && volume >= 36);
    return heavy ? BATTERY_LOAD_HEAVY : BATTERY_LOAD_LIGHT;
}

void battery_follow(AppScreens *screens, HalBattery battery, uint32_t now_ms, bool display_on)
{
    BatteryEstimate *estimate = &screens->context.settings->battery;
    if (estimate->sampled && now_ms - estimate->last_sample_ms < BATTERY_ESTIMATE_SAMPLE_MS &&
        battery.usb_power == estimate->on_usb) {
        return;
    }
    battery_estimate_update(
        estimate, (BatteryEstimateReading){.now_ms = now_ms,
                                           .millivolts = battery.millivolts,
                                           .load = load_class(&screens->context, display_on),
                                           .known = battery.known,
                                           .usb_power = battery.usb_power});
}
