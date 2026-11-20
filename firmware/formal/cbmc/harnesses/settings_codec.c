#include "formal.h"
#include "settings/settings.h"

static uint8_t bytes[FORMAL_CARD_BYTES];

void harness(void)
{
    Settings settings;
    __CPROVER_havoc_object(bytes);
    uint32_t length = formal_below(FORMAL_CARD_BYTES + 1);
    if (settings_decode(bytes, length, &settings)) {
        __CPROVER_assert(settings.language[SETTINGS_LANGUAGE_LENGTH] == '\0',
                         "a decoded language code ends inside its field");
        __CPROVER_assert(settings.equalizer < EQUALIZER_PRESET_COUNT, "a decoded preset is known");
        uint8_t encoded[SETTINGS_FILE_SIZE];
        settings_encode(&settings, encoded);
    }
    Settings buttons = settings_defaults();
    (void)settings_buttons_decode(bytes, length, &buttons);
    (void)settings_file_newer(bytes, length);
}
