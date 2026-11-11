#pragma once

#include "mixtape_ui.h"

enum {
    MIXTAPE_LIST,
    MIXTAPE_ACTIONS,
    MIXTAPE_NAME,
    MIXTAPE_CONFIRM_DELETE,
    MIXTAPE_LENGTH,
    MIXTAPE_PICK,
    MIXTAPE_SIDE
};
enum {
    MIXTAPE_SNAPSHOT = 1,
    MIXTAPE_STORE,
    MIXTAPE_OPEN,
    MIXTAPE_RESOLVE,
    MIXTAPE_COPY
};

Screen *mixtape_ui_add(MixtapeUi *ui, uint16_t track);
bool mixtape_ui_append(MixtapeUi *ui);

Screen *mixtape_ui_init(MixtapeUi *ui, MixtapeUiConfig config);
bool mixtape_ui_busy(const MixtapeUi *ui);
void mixtape_ui_step(MixtapeUi *ui, uint32_t now_ms);
void mixtape_ui_cancel(MixtapeUi *ui);
void mixtape_ui_refresh(MixtapeUi *ui);
void mixtape_ui_message(MixtapeUi *ui, const char *text);
bool mixtape_ui_save(MixtapeUi *ui);
bool mixtape_ui_load(MixtapeUi *ui);

const char *mixtape_ui_name_view(const char *name, const Font *font, int width);
