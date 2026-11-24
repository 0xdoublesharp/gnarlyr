#include "device/tick_clock.h"

enum {
    MICROSECONDS_PER_MILLISECOND = 1000
};

static uint32_t ticks_per_millisecond(const TickClock *clock)
{
    return clock->ticks_per_microsecond * MICROSECONDS_PER_MILLISECOND;
}

static uint32_t observed_ticks(TickClock *clock, TickReading reading)
{
    uint32_t position = (clock->period - reading.counter % clock->period) % clock->period;
    uint32_t ticks = reading.reload_pending ? clock->period + position : position;
    if (ticks > clock->observed_ticks) {
        clock->observed_ticks = ticks;
    }
    return clock->observed_ticks;
}

static void add_ticks(TickClock *clock, uint32_t ticks)
{
    clock->millisecond_ticks += ticks;
    clock->milliseconds += clock->millisecond_ticks / ticks_per_millisecond(clock);
    clock->millisecond_ticks %= ticks_per_millisecond(clock);
}

void tick_clock_init(TickClock *clock, uint32_t ticks_per_microsecond, uint32_t period)
{
    *clock = (TickClock){.ticks_per_microsecond = ticks_per_microsecond, .period = period};
}

void tick_clock_reload(TickClock *clock)
{
    add_ticks(clock, clock->period);
    clock->observed_ticks =
        clock->observed_ticks > clock->period ? clock->observed_ticks - clock->period : 0;
}

void tick_clock_set_rate(TickClock *clock, TickReading reading, uint32_t ticks_per_microsecond,
                         uint32_t period)
{
    add_ticks(clock, observed_ticks(clock, reading));
    clock->observed_ticks = 0;
    uint32_t old_rate = clock->ticks_per_microsecond;
    clock->millisecond_ticks = clock->millisecond_ticks * ticks_per_microsecond / old_rate;
    clock->ticks_per_microsecond = ticks_per_microsecond;
    clock->period = period;
}

uint32_t tick_clock_milliseconds(TickClock *clock, TickReading reading)
{
    uint32_t ticks = clock->millisecond_ticks + observed_ticks(clock, reading);
    return clock->milliseconds + ticks / ticks_per_millisecond(clock);
}

uint32_t tick_clock_microseconds(TickClock *clock, TickReading reading)
{
    uint32_t ticks = clock->millisecond_ticks + observed_ticks(clock, reading);
    return clock->milliseconds * MICROSECONDS_PER_MILLISECOND +
           ticks / clock->ticks_per_microsecond;
}
