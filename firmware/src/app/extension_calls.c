#include <stddef.h>

#include "core/seam.h"
#include "extension_entry.h"
#include "hal/audio.h"
#include "hal/storage.h"
#include "hal/system.h"

static const ExtensionModuleEntry *extension(void)
{
    return hal_system_extension_code();
}

void wire_motion_run(WireMotion *motion, const void *owner, WireShape shape, uint32_t now_ms)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->motion_run(motion, owner, shape, now_ms);
    }
}

void wire_motion_progress(WireMotion *motion, uint32_t done, uint32_t total)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->motion_progress(motion, done, total);
    }
}

void wire_motion_end(WireMotion *motion, const void *owner)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->motion_end(motion, owner);
    }
}

bool wire_motion_shown(const WireMotion *motion, const void *owner)
{
    const ExtensionModuleEntry *code = extension();
    return code != NULL && code->motion_shown(motion, owner);
}

CORE_SEAM bool wire_motion_due(const WireMotion *motion, uint32_t now_ms)
{
    const ExtensionModuleEntry *code = extension();
    return code != NULL && code->motion_due(motion, now_ms);
}

void wire_motion_tick(WireMotion *motion, uint32_t now_ms)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->motion_tick(motion, now_ms);
    }
}

Rect wire_motion_fit(Rect space)
{
    const ExtensionModuleEntry *code = extension();
    return code != NULL ? code->motion_fit(space) : (Rect){space.x, space.y, 0, 0};
}

void wire_motion_draw(WireMotion *motion, Canvas *canvas, const Theme *theme, Rect space)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->motion_draw(motion, canvas, theme, space);
    }
}

bool hal_file_remove(const char *path)
{
    const ExtensionModuleEntry *code = extension();
    return code != NULL && code->file_remove(path);
}

void headphone_store_load(HeadphoneStore *store)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->headphones_load(store);
    } else {
        *store = (HeadphoneStore){.profiles = {.current = HEADPHONE_PROFILE_NONE},
                                  .active = {.left = REPLAY_GAIN_UNITY, .right = REPLAY_GAIN_UNITY},
                                  .load = STORE_UNREADABLE};
        store->file.needs_reload = true;
        hal_audio_set_headphones(store->active);
    }
}

bool hal_clock_set_wall(HalWallTime time)
{
    const ExtensionModuleEntry *code = extension();
    return code != NULL && code->clock_set(time);
}

bool hal_clock_restore_wall(int16_t offset_minutes)
{
    const ExtensionModuleEntry *code = extension();
    return code != NULL && code->clock_restore(offset_minutes);
}

bool app_menu_hold(struct AppScreens *screens, KeyEvent event)
{
    const ExtensionModuleEntry *code = extension();
    return code != NULL && code->menu_hold(screens, event);
}
