#include "check.h"
#include "device/headset_monitor.h"

enum {
    SAMPLE_MS = 5,
    IDLE_LEVEL = 1023,
    DIP_LEVEL = 900
};

typedef struct Rig {
    HeadsetMonitor monitor;
    uint32_t now_ms;
} Rig;

static void feed(Rig *rig, uint16_t level, bool plugged, uint32_t duration_ms)
{
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += SAMPLE_MS) {
        headset_monitor_sample(&rig->monitor, level, plugged, rig->now_ms);
        rig->now_ms += SAMPLE_MS;
    }
}

static void feed_jitter(Rig *rig, uint32_t duration_ms)
{
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += SAMPLE_MS) {
        uint16_t level = (elapsed / SAMPLE_MS) % 3 == 0 ? 1 : 0;
        headset_monitor_sample(&rig->monitor, level, true, rig->now_ms);
        rig->now_ms += SAMPLE_MS;
    }
}

static void dip_train(Rig *rig, int dips)
{
    for (int dip = 0; dip < dips; dip++) {
        feed(rig, DIP_LEVEL, true, 10);
        feed(rig, IDLE_LEVEL, true, 30);
    }
}

static HalHeadsetReading reading(const Rig *rig)
{
    return headset_monitor_reading(&rig->monitor, rig->now_ms);
}

static Rig remote_rig(void)
{
    Rig rig = {.now_ms = 1000};
    headset_monitor_init(&rig.monitor);
    feed(&rig, IDLE_LEVEL, false, 1000);
    feed(&rig, IDLE_LEVEL, true, 1000);
    return rig;
}

static void check_no_mic(const Rig *rig)
{
    HalHeadsetReading now = reading(rig);
    CHECK(now.no_mic);
    CHECK(!now.dipped);
    CHECK(now.level <= 1);
}

static void a_three_pole_plug_reads_as_no_mic_without_dips(void)
{
    Rig rig = {.now_ms = 1000};
    headset_monitor_init(&rig.monitor);
    feed(&rig, IDLE_LEVEL, false, 1000);
    feed(&rig, IDLE_LEVEL, true, 60);
    feed_jitter(&rig, 60000);
    check_no_mic(&rig);
    feed(&rig, IDLE_LEVEL, false, 100);
    HalHeadsetReading after = reading(&rig);
    CHECK(!after.no_mic);
    CHECK(!after.dipped);
    CHECK_EQUAL(IDLE_LEVEL, after.level);
    CHECK_EQUAL(HAL_HEADSET_BUTTON_NONE, after.button);
}

static void a_remote_counts_a_train_of_dips(void)
{
    Rig rig = remote_rig();
    dip_train(&rig, 4);
    HalHeadsetReading now = reading(&rig);
    CHECK(!now.no_mic);
    CHECK(now.dipped);
    CHECK_EQUAL(4, now.dips);
    CHECK_EQUAL(DIP_LEVEL, now.lowest);
    CHECK_EQUAL(10, now.dip_ms);
}

static void swapping_a_remote_for_a_three_pole_plug_shows_no_mic(void)
{
    Rig rig = remote_rig();
    dip_train(&rig, 4);
    feed(&rig, IDLE_LEVEL, false, 200);
    feed(&rig, IDLE_LEVEL, true, 40);
    feed_jitter(&rig, 60000);
    check_no_mic(&rig);
}

static void an_unseen_swap_to_a_three_pole_plug_still_shows_no_mic(void)
{
    Rig rig = remote_rig();
    dip_train(&rig, 4);
    feed_jitter(&rig, HEADSET_REMOTE_PLUG_CHANGE_MS + 100);
    check_no_mic(&rig);
    feed_jitter(&rig, 60000);
    check_no_mic(&rig);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_three_pole_plug_reads_as_no_mic_without_dips),
        TEST_CASE(a_remote_counts_a_train_of_dips),
        TEST_CASE(swapping_a_remote_for_a_three_pole_plug_shows_no_mic),
        TEST_CASE(an_unseen_swap_to_a_three_pole_plug_still_shows_no_mic),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
