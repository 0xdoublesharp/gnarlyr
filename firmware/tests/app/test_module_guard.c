#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#include "app_screens.h"
#include "check.h"
#include "fake_hal.h"
#include "hal/system.h"
#include "module_guard.h"
#include "modules/modules.h"
#include "screen_modules.h"

static jmp_buf escape;
static int trapped;

static void escape_trap(const char *report, const void *function)
{
    (void)report;
    (void)function;
    trapped++;
    longjmp(escape, 1);
}

static void reset(void)
{
    fake_hal_reset();
    trapped = 0;
}

static bool call_trapped(void (*call)(void))
{
    module_guard_set_trap(escape_trap);
    if (setjmp(escape) == 0) {
        call();
    }
    module_guard_set_trap(NULL);
    return trapped > 0;
}

static ScreenAction (*kept_hold)(AppScreens *screens, uint8_t action);

static void keep_settings_hold(void)
{
    const SettingsModuleEntry *entry = hal_system_load_module(SCREEN_MODULE_SETTINGS);
    CHECK(entry != NULL);
    kept_hold = entry->menu_hold;
}

static void stale_settings_hold(void)
{
    (void)kept_hold(NULL, 0);
}

static void module_code_lives_in_guarded_sections(void)
{
    reset();
    CHECK(module_guard_guarded_sets() > 0);
    CHECK((module_guard_owners((const void *)(uintptr_t)SETTINGS_MODULE_ENTRY.menu_hold) &
           1u << SCREEN_MODULE_SETTINGS) != 0);
    CHECK((module_guard_owners((const void *)(uintptr_t)EQUALIZER_MODULE_ENTRY.load) &
           1u << SCREEN_MODULE_EQUALIZER) != 0);
    CHECK_EQUAL(0, module_guard_owners((const void *)(uintptr_t)hal_system_load_module));
    CHECK(module_guard_probe(SCREEN_MODULE_SETTINGS) != NULL);
}

static void a_call_into_an_evicted_module_is_trapped(void)
{
    reset();
    keep_settings_hold();
    CHECK(hal_system_load_module(SCREEN_MODULE_VIEWS) != NULL);
    CHECK(call_trapped(stale_settings_hold));
}

static void a_module_that_was_never_loaded_is_not_checked(void)
{
    reset();
    CHECK(hal_system_load_module(SCREEN_MODULE_VIEWS) != NULL);
    CHECK(module_guard_live(SCREEN_MODULE_SETTINGS));
    CHECK(!module_guard_evicted((const void *)&SETTINGS_MODULE_ENTRY));
}

static void eviction_poisons_and_a_reload_clears_the_module_memory(void)
{
    reset();
    const void *probe = module_guard_probe(SCREEN_MODULE_SETTINGS);
    CHECK(hal_system_load_module(SCREEN_MODULE_SETTINGS) != NULL);
    CHECK(hal_system_load_module(SCREEN_MODULE_LIBRARY) != NULL);
    CHECK(module_guard_evicted(probe));
    CHECK(hal_system_load_module(SCREEN_MODULE_SETTINGS) != NULL);
    CHECK(!module_guard_evicted(probe));
    CHECK_EQUAL(0, ((const volatile uint8_t *)probe)[0]);
}

static void usb_rescan_and_refused_loads_evict_the_slot(void)
{
    reset();
    keep_settings_hold();
    fake_hal_load_usb_overlay();
    CHECK(call_trapped(stale_settings_hold));
    CHECK(hal_system_load_module(SCREEN_MODULE_SETTINGS) != NULL);
    (void)hal_system_load_rescan_code();
    CHECK(module_guard_evicted((const void *)(uintptr_t)kept_hold));
    CHECK(hal_system_load_module(SCREEN_MODULE_SETTINGS) != NULL);
    fake_hal_break_module(SCREEN_MODULE_VIEWS, true);
    CHECK(hal_system_load_module(SCREEN_MODULE_VIEWS) == NULL);
    CHECK(module_guard_evicted((const void *)(uintptr_t)kept_hold));
}

static void playing_and_stage_share_their_common_code(void)
{
    reset();
    const PlayingModuleEntry *playing = hal_system_load_module(SCREEN_MODULE_PLAYING);
    CHECK(playing != NULL);
    const void *section = (const void *)(uintptr_t)playing->section;
    const void *strip = (const void *)(uintptr_t)playing->artist_strip;
    CHECK(hal_system_load_module(SCREEN_MODULE_STAGE) != NULL);
    CHECK(!module_guard_evicted(section));
    CHECK(module_guard_evicted(strip));
}

int main(void)
{
    static const TestCase CASES[] = {
        TEST_CASE(module_code_lives_in_guarded_sections),
        TEST_CASE(a_call_into_an_evicted_module_is_trapped),
        TEST_CASE(a_module_that_was_never_loaded_is_not_checked),
        TEST_CASE(eviction_poisons_and_a_reload_clears_the_module_memory),
        TEST_CASE(usb_rescan_and_refused_loads_evict_the_slot),
        TEST_CASE(playing_and_stage_share_their_common_code),
    };
    return test_run_all(CASES, sizeof CASES / sizeof CASES[0]);
}
