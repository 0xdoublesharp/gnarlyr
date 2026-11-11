#pragma once

#include "headphone_store.h"
#include "ui/list_view.h"
#include "ui/screen.h"
#include "ui/toast.h"

typedef struct HeadphoneEditorConfig {
    HeadphoneStore *store;
    Player *player;
    SettingsStore *settings;
    const Theme *theme;
    Toast *toast;
} HeadphoneEditorConfig;

typedef struct HeadphoneEditor {
    Screen screen;
    HeadphoneEditorConfig config;
    ListModel model;
    ListView list;
    uint32_t now_ms;
    uint8_t mode;
    uint8_t field;
    uint8_t slot;
    char *label_text;
    char *detail_text;
    char label[64];
    char detail[64];
    char message[64];
} HeadphoneEditor;

Screen *headphone_editor_init(HeadphoneEditor *editor, HeadphoneEditorConfig config);
