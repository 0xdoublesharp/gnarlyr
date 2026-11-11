#include "modules/modules.h"

static void bind(AppScreens *screens)
{
    const AppScreensContext *context = &screens->context;
    equalizer_ui_init(&screens->modules.equalizer, context->settings, context->player,
                      context->toast, context->theme);
}

const ParametricModuleEntry EQUALIZER_MODULE_ENTRY = {
    .module = {SCREEN_MODULE_MAGIC(SCREEN_MODULE_EQUALIZER), bind}, .load = parametric_store_read};
