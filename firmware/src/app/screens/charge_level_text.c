#include "charge_screen.h"

#include "core/i18n.h"
#include "core/string_builder.h"

void charge_screen_level_text(HalBattery battery, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    if (battery.known) {
        i18n_format_counts(&builder, STR_POWER_PERCENT,
                           battery.percent > 100 ? 100 : battery.percent, 0);
    } else {
        string_builder_append(&builder, i18n_text(STR_POWER_UNKNOWN));
    }
}
