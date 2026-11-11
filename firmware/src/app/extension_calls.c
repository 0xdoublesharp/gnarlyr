#include <stddef.h>

#include "core/seam.h"
#include "extension_entry.h"
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

void home_art_init(HomeArt *art, CoverCache *covers, const LibraryStore *store, Player *player,
                   const Theme *theme)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->art_init(art, covers, store, player, theme);
    }
}

void home_art_update(void *context, uint32_t now_ms)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->art_update(context, now_ms);
    }
}

void home_art_draw(void *context, Canvas *canvas, Rect area)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->art_draw(context, canvas, area);
    }
}

void home_art_draw_backdrop(Canvas *canvas, Rect area, const Theme *theme)
{
    const ExtensionModuleEntry *code = extension();
    if (code != NULL) {
        code->art_backdrop(canvas, area, theme);
    }
}
