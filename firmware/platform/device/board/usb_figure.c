#include "extension_entry.h"
#include "hal/system.h"

void wire_figure_draw(Canvas *canvas, const Theme *theme, Rect area, WireFigureFrame frame)
{
    const ExtensionModuleEntry *code = hal_system_extension_code();
    if (code != NULL) {
        code->figure_draw(canvas, theme, area, frame);
    }
}
