#include "charge_rate.h"

static int distance(int first, int second)
{
    return first > second ? first - second : second - first;
}

static void restart(ChargeRate *rate, HalBattery battery, uint32_t now_ms, bool computer)
{
    *rate = (ChargeRate){.sampled_ms = now_ms,
                         .count = 1,
                         .next = 1,
                         .active = battery.known && battery.usb_power && battery.charging &&
                                   battery.percent <= 100,
                         .computer = computer};
    rate->levels[0] = battery.percent;
    rate->times[0] = now_ms;
}

static void estimate(ChargeRate *rate, uint8_t percent)
{
    int oldest = rate->count == CHARGE_RATE_WINDOW ? rate->next : 0;
    int gain = percent - rate->levels[oldest];
    uint32_t span = rate->sampled_ms - rate->times[oldest];
    if (span < 300000 || gain < 2) {
        rate->filtered = 0;
        rate->rate = 0;
        rate->agreement = 0;
        rate->stable_windows = 0;
        rate->minutes = 0;
        return;
    }
    int raw = (int)((uint32_t)gain * 3600000u / span);
    int quantization = (int)(3600000u / span);
    int tolerance = raw / 4 > quantization ? raw / 4 : quantization;
    rate->agreement = distance(raw, rate->candidate) <= tolerance ? rate->agreement + 1 : 1;
    if (rate->agreement > 3) {
        rate->agreement = 3;
    }
    rate->candidate = (uint8_t)raw;
    if (rate->agreement < 3) {
        rate->filtered = 0;
        rate->rate = 0;
        rate->minutes = 0;
        rate->stable_windows = 0;
        return;
    }
    rate->filtered =
        (uint16_t)(rate->filtered == 0 ? raw * 4 : (3 * rate->filtered + raw * 4 + 2) / 4);
    int smoothed = ((rate->filtered + 6) / 12) * 3;
    if (distance(smoothed, rate->rate) >= 3) {
        rate->rate = (uint8_t)smoothed;
    }
    if (rate->stable_windows < 3) {
        rate->stable_windows++;
    }
    int minutes = ((100 - percent) * 60 / rate->rate + 4) / 5 * 5;
    if (rate->stable_windows >= 3 && distance(minutes, rate->minutes) >= 5) {
        rate->minutes = (uint16_t)minutes;
    }
}

void charge_rate_update(ChargeRate *rate, HalBattery battery, uint32_t now_ms, bool computer)
{
    bool active = battery.known && battery.usb_power && battery.charging && battery.percent <= 100;
    uint32_t elapsed = now_ms - rate->sampled_ms;
    if (!active || !rate->active || computer != rate->computer || elapsed > 90000) {
        restart(rate, battery, now_ms, computer);
        return;
    }
    if (elapsed < CHARGE_RATE_MINUTE_MS) {
        return;
    }
    int previous = (rate->next + CHARGE_RATE_WINDOW - 1) % CHARGE_RATE_WINDOW;
    int change = battery.percent - rate->levels[previous];
    if (change < 0 || change > 3 || battery.percent > 100) {
        restart(rate, battery, now_ms, computer);
        return;
    }
    rate->sampled_ms = now_ms;
    rate->times[rate->next] = now_ms;
    rate->levels[rate->next] = battery.percent;
    rate->next = (uint8_t)((rate->next + 1) % CHARGE_RATE_WINDOW);
    if (rate->count < CHARGE_RATE_WINDOW) {
        rate->count++;
    }
    estimate(rate, battery.percent);
}
