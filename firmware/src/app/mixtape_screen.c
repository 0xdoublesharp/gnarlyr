#include "mixtape_ui_private.h"

#include <stddef.h>

#include "core/string_builder.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"
#include "ui/message_screen.h"

static const char CHARACTERS[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!&()";
static const char *const ACTIONS[] = {"Play", "Edit in Up Next", "Replace with current queue",
                                      "Rename", "Delete"};

static void name_copy(char *to, const char *from)
{
    StringBuilder builder = string_builder_make(to, MIX_NAME_BYTES);
    string_builder_append(&builder, from);
}

static int row_count(const void *context)
{
    const MixtapeUi *ui = context;
    return ui->mode == MIXTAPE_ACTIONS ? 5 : ui->mode == MIXTAPE_CONFIRM_DELETE ? 2 : ui->count + 2;
}

static const char *row_label(const void *context, int index)
{
    const MixtapeUi *ui = context;
    if (ui->mode == MIXTAPE_ACTIONS) {
        return ACTIONS[index];
    }
    if (ui->mode == MIXTAPE_CONFIRM_DELETE) {
        return index == 0 ? "Keep mixtape" : "Delete mixtape";
    }
    if (index < 2) {
        return index == 0 ? "Save current queue" : "New empty mixtape";
    }
    int row = index - ui->rows_first;
    return row >= 0 && row < MIXTAPE_VISIBLE_ROWS ? ui->labels[row] : "";
}

static void refresh_labels(MixtapeUi *ui)
{
    if (ui->mode != MIXTAPE_LIST || ui->rows_first == ui->list.top) {
        return;
    }
    ui->rows_first = ui->list.top;
    for (int row = 0; row < MIXTAPE_VISIBLE_ROWS; row++) {
        int index = ui->rows_first + row - 2;
        ui->labels[row][0] = 0;
        MixEntry entry;
        if (index >= 0 && index < ui->count &&
            mixtape_store_read(&ui->store, ui->slots[index], &entry)) {
            name_copy(ui->labels[row], entry.name);
        }
    }
}

void mixtape_ui_refresh(MixtapeUi *ui)
{
    mixtape_store_init(&ui->store);
    ui->count = 0;
    for (uint16_t slot = 0; slot < MIX_CATALOG_SLOTS; slot++) {
        MixEntry entry;
        if (mixtape_store_read(&ui->store, slot, &entry) && entry.active) {
            ui->slots[ui->count++] = (uint8_t)slot;
        }
    }
    ui->rows_first = -1;
    ui->screen.title = ui->mode == MIXTAPE_LIST   ? "Mixtapes"
                       : ui->mode == MIXTAPE_NAME ? "Mixtape name"
                                                  : ui->name;
    list_view_select(&ui->list, ui->list.selected, ui->now_ms);
    refresh_labels(ui);
}

static void enter_mode(MixtapeUi *ui, uint8_t mode)
{
    ui->mode = mode;
    ui->screen.title = mode == MIXTAPE_LIST   ? "Mixtapes"
                       : mode == MIXTAPE_NAME ? "Mixtape name"
                                              : ui->name;
    ui->rows_first = -1;
    list_view_init(&ui->list, &ui->model, ui->config.theme, ui->now_ms);
    refresh_labels(ui);
}

static bool new_slot(MixtapeUi *ui)
{
    for (uint16_t slot = 0; slot < MIX_CATALOG_SLOTS; slot++) {
        MixEntry entry;
        if (!mixtape_store_read(&ui->store, slot, &entry)) {
            break;
        }
        if (!entry.active) {
            ui->slot = slot;
            StringBuilder builder = string_builder_make(ui->name, sizeof ui->name);
            string_builder_append(&builder, "Mixtape ");
            string_builder_append_unsigned(&builder, slot + 1u);
            return true;
        }
    }
    mixtape_ui_message(ui, ui->count == MIX_CATALOG_SLOTS ? "All mixtape slots are full"
                                                          : "Could not read mixtapes");
    return false;
}

static bool start_save(MixtapeUi *ui)
{
    if (!mixtape_ui_save(ui)) {
        mixtape_ui_message(ui, "Could not save mixtape");
        return false;
    }
    return true;
}

static ScreenAction select_action(MixtapeUi *ui, int index)
{
    if (index == 0 || index == 1) {
        ui->loading = index == 0;
        if (mixtape_ui_load(ui)) {
            return screen_action_push(index == 0 ? ui->config.now_playing : ui->config.up_next);
        }
        mixtape_ui_message(ui, "Could not open mixtape");
    } else if (index == 2) {
        ui->save_queue = true;
        ui->renaming = false;
        start_save(ui);
    } else if (index == 3) {
        ui->renaming = true;
        enter_mode(ui, MIXTAPE_NAME);
    } else {
        enter_mode(ui, MIXTAPE_CONFIRM_DELETE);
    }
    return screen_action_none();
}

static ScreenAction select_row(void *context, int index)
{
    MixtapeUi *ui = context;
    if (ui->mode == MIXTAPE_ACTIONS) {
        return select_action(ui, index);
    }
    if (ui->mode == MIXTAPE_CONFIRM_DELETE) {
        if (index == 0) {
            enter_mode(ui, MIXTAPE_ACTIONS);
        } else if (mixtape_store_change(&ui->store, (MixtapeChange){.operation = MIXTAPE_DELETE,
                                                                    .slot = ui->slot})) {
            ui->stage = MIXTAPE_STORE;
        } else {
            mixtape_ui_message(ui, "Could not delete mixtape");
        }
        return screen_action_none();
    }
    if (index < 2) {
        if (index == 0 && !ui->config.player->has_track) {
            mixtape_ui_message(ui, "Nothing is playing");
        } else if (new_slot(ui)) {
            ui->save_queue = index == 0;
            ui->renaming = false;
            enter_mode(ui, MIXTAPE_NAME);
        }
    } else if (index < ui->count + 2) {
        MixEntry entry;
        ui->slot = ui->slots[index - 2];
        if (mixtape_store_read(&ui->store, ui->slot, &entry)) {
            name_copy(ui->name, entry.name);
            enter_mode(ui, MIXTAPE_ACTIONS);
        }
    }
    return screen_action_none();
}

static void edit_name(MixtapeUi *ui, KeyEvent event)
{
    if (event.key == KEY_VOLUME_UP || event.key == KEY_VOLUME_DOWN) {
        int direction = event.key == KEY_VOLUME_UP ? 1 : -1;
        int count = (int)sizeof CHARACTERS - 1;
        ui->character = (uint8_t)((ui->character + direction + count) % count);
    } else if (event.key == KEY_PLAY && event.gesture == KEY_GESTURE_LONG_PRESS) {
        start_save(ui);
    } else if (event.key == KEY_PLAY && event.gesture == KEY_GESTURE_PRESS) {
        char old[MIX_NAME_BYTES];
        name_copy(old, ui->name);
        StringBuilder builder = string_builder_make(ui->name, sizeof ui->name);
        string_builder_append(&builder, old);
        string_builder_append_char(&builder, CHARACTERS[ui->character]);
    } else if (event.key == KEY_MENU && event.gesture == KEY_GESTURE_PRESS) {
        int length = 0;
        while (ui->name[length] != 0) {
            length++;
        }
        if (length > 0) {
            do {
                length--;
            } while (length > 0 && ((uint8_t)ui->name[length] & 0xc0u) == 0x80u);
            ui->name[length] = 0;
        }
    }
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    MixtapeUi *ui = context;
    ui->now_ms = now_ms;
    if (event.key == KEY_BACK && event.gesture == KEY_GESTURE_PRESS && ui->mode != MIXTAPE_LIST) {
        enter_mode(ui, MIXTAPE_LIST);
        return screen_action_none();
    }
    if (ui->mode == MIXTAPE_NAME) {
        edit_name(ui, event);
        return screen_action_none();
    }
    ListSelection selection = {.select = select_row, .context = ui};
    ScreenAction action = list_navigation_handle(&ui->list, event, now_ms, selection);
    refresh_labels(ui);
    return action;
}

static void enter(void *context, uint32_t now_ms)
{
    MixtapeUi *ui = context;
    ui->now_ms = now_ms;
    if (!mixtape_ui_busy(ui)) {
        enter_mode(ui, MIXTAPE_LIST);
        mixtape_ui_refresh(ui);
    }
}

static void update(void *context, uint32_t now_ms)
{
    MixtapeUi *ui = context;
    ui->now_ms = now_ms;
    refresh_labels(ui);
}

static void draw_name(const MixtapeUi *ui, Canvas *canvas)
{
    const Theme *theme = ui->config.theme;
    Rect area = theme_content_area(theme);
    canvas_fill_rect(canvas, area, theme->colors.background);
    TextStyle style = {.font = theme->fonts.body, .color = theme->colors.text};
    area.x += theme->metrics.padding;
    area.y += theme->metrics.padding;
    area.width -= 2 * theme->metrics.padding;
    area.height = theme->metrics.row_height;
    label_draw(canvas, style, (Label){area, ui->name});
    char selected[] = {'[', ' ', ' ', ' ', ']', 0};
    selected[2] = CHARACTERS[ui->character];
    area.y += area.height;
    label_draw(canvas, style, (Label){area, selected});
    style.font = theme->fonts.small;
    style.color = theme->colors.text_secondary;
    area.y += area.height;
    label_draw(canvas, style, (Label){area, "Volume: choose   Play: add   Menu: erase"});
    area.y += area.height;
    label_draw(canvas, style, (Label){area, "Hold Play to save   Back to cancel"});
}

static void draw(void *context, Canvas *canvas)
{
    const MixtapeUi *ui = context;
    if (mixtape_ui_busy(ui)) {
        MessageScreen message;
        Screen *screen = message_screen_init(
            &message,
            (MessageConfig){ui->stage >= MIXTAPE_OPEN ? "Opening mixtape" : "Saving mixtape",
                            "Back cancels", ui->config.theme});
        screen->draw(screen->context, canvas);
        return;
    }
    header_draw(canvas, ui->config.theme, ui->screen.title);
    if (ui->mode == MIXTAPE_NAME) {
        draw_name(ui, canvas);
    } else {
        list_view_draw(&ui->list, canvas, theme_content_area(ui->config.theme), ui->now_ms);
    }
}

Screen *mixtape_ui_init(MixtapeUi *ui, MixtapeUiConfig config)
{
    *ui = (MixtapeUi){.config = config, .rows_first = -1};
    ui->screen = (Screen){.title = "Mixtapes",
                          .context = ui,
                          .takes_every_key = true,
                          .stays_on_wake = true,
                          .enter = enter,
                          .handle_key = handle_key,
                          .update = update,
                          .draw = draw};
    ui->model = (ListModel){.context = ui, .count = row_count, .label = row_label};
    list_view_init(&ui->list, &ui->model, config.theme, 0);
    mixtape_ui_refresh(ui);
    return &ui->screen;
}
