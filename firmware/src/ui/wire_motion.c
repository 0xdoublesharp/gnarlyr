#include "ui/wire_motion.h"
#include "core/seam.h"

enum {
    MOTION_PERCENT = 100
};

void wire_motion_run(WireMotion *motion, const void *owner, WireShape shape, uint32_t now_ms)
{
    if (motion->running && motion->owner == owner && motion->shape == shape) {
        return;
    }
    *motion = (WireMotion){.owner = owner,
                           .started_ms = now_ms,
                           .shape = (uint8_t)shape,
                           .percent = WIRE_MOTION_ENDLESS,
                           .running = true};
}

void wire_motion_progress(WireMotion *motion, uint32_t done, uint32_t total)
{
    uint32_t capped = done < total ? done : total;
    motion->percent = total == 0 ? WIRE_MOTION_ENDLESS : (uint8_t)(capped * MOTION_PERCENT / total);
}

void wire_motion_end(WireMotion *motion, const void *owner)
{
    if (motion->owner == owner) {
        motion->running = false;
    }
}

bool wire_motion_shown(const WireMotion *motion, const void *owner)
{
    return motion->running && motion->ticked && motion->owner == owner;
}

CORE_SEAM bool wire_motion_due(const WireMotion *motion, uint32_t now_ms)
{
    if (!motion->running || now_ms - motion->started_ms < WIRE_MOTION_DELAY_MS) {
        return false;
    }
    return !motion->ticked || now_ms - motion->ticked_ms >= WIRE_MOTION_FRAME_MS;
}

void wire_motion_tick(WireMotion *motion, uint32_t now_ms)
{
    uint32_t since = now_ms - motion->started_ms;
    motion->turn = (uint8_t)(since / WIRE_MOTION_TURN_MS);
    motion->ticked = true;
    motion->ticked_ms = now_ms - (since - WIRE_MOTION_DELAY_MS) % WIRE_MOTION_FRAME_MS;
}

Rect wire_motion_fit(Rect space)
{
    int side = space.width < space.height ? space.width : space.height;
    side = side > WIRE_MOTION_MAX_SIDE ? WIRE_MOTION_MAX_SIDE : side;
    if (side < WIRE_MOTION_MIN_SIDE) {
        return (Rect){space.x, space.y, 0, 0};
    }
    return (Rect){space.x + (space.width - side) / 2, space.y + (space.height - side) / 2, side,
                  side};
}

void wire_motion_draw(WireMotion *motion, Canvas *canvas, const Theme *theme, Rect space)
{
    Rect area = wire_motion_fit(space);
    motion->area = area;
    if (!motion->running || !motion->ticked) {
        return;
    }
    WireFigureFrame frame = {motion->shape, motion->turn, motion->percent};
    wire_figure_draw(canvas, theme, area, frame);
}
