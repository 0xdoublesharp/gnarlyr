#include "parametric_screen.h"

#include "core/string_builder.h"

enum {
    RESET_ROW = SOUND_SHAPING_COUNT,
    SAVE_ROW,
    BASE_ROW
};

Rect sound_shaping_screen_slider(const ParametricScreen *editor, Canvas *canvas, Rect area)
{
    if (editor->list.selected >= SOUND_SHAPING_COUNT) {
        return area;
    }
    const Theme *theme = editor->config.theme;
    int height = theme->metrics.padding;
    int half = height / 2;
    int left = area.x + half;
    int width = area.width - height;
    canvas_fill_rect(canvas, (Rect){left, area.y + half, width, theme->metrics.scrollbar_inset},
                     theme->colors.text_tertiary);
    for (int tick = 0; tick <= 2 * SOUND_SHAPING_LIMIT; tick++) {
        int x = left + width * tick / (2 * SOUND_SHAPING_LIMIT);
        canvas_fill_rect(canvas, (Rect){x, area.y + half / 2, theme->metrics.scrollbar_inset, half},
                         theme->colors.text_tertiary);
    }
    int step = editor->shaping.positions[editor->list.selected] + SOUND_SHAPING_LIMIT;
    int x = left + width * step / (2 * SOUND_SHAPING_LIMIT);
    canvas_fill_rect(canvas, (Rect){x - half / 2, area.y, half, height},
                     editor->adjusting ? theme->colors.accent_alt : theme->colors.accent);
    area.y += height + half;
    area.height -= height + half;
    return area;
}

static void append_step(StringBuilder *builder, int step)
{
    string_builder_append_char(builder, step < 0 ? '-' : '+');
    string_builder_append_unsigned(builder, (uint32_t)(step < 0 ? -step : step));
}

void sound_shaping_screen_texts(ParametricScreen *editor)
{
    for (int slider = 0; slider < SOUND_SHAPING_COUNT; slider++) {
        StringBuilder label = string_builder_make(editor->labels[slider], 48);
        string_builder_append(&label, i18n_text(sound_shaping_label(slider)));
        StringBuilder detail = string_builder_make(editor->details[slider], 48);
        string_builder_append(&detail, i18n_text(sound_shaping_end(slider, false)));
        string_builder_append_char(&detail, ' ');
        append_step(&detail, editor->shaping.positions[slider]);
        string_builder_append_char(&detail, ' ');
        string_builder_append(&detail, i18n_text(sound_shaping_end(slider, true)));
    }
    static const StringId actions[] = {STR_SHAPE_RESET, STR_EQ_SAVE_TO_SLOT, STR_SHAPE_BASE};
    for (int index = 0; index < 3; index++) {
        StringBuilder builder = string_builder_make(editor->labels[RESET_ROW + index], 48);
        string_builder_append(&builder, i18n_text(actions[index]));
        editor->details[RESET_ROW + index][0] = '\0';
    }
    StringBuilder builder = string_builder_make(editor->details[SAVE_ROW], 48);
    string_builder_append_unsigned(&builder, (uint32_t)editor->slot + 1);
    builder = string_builder_make(editor->details[BASE_ROW], 48);
    if (editor->base_slot < 0) {
        string_builder_append(
            &builder, i18n_text(editor->shaping.base_count == 0 ? STR_SHAPE_NONE : STR_PEQ));
    } else {
        i18n_format_counts(&builder, STR_SHAPE_SLOT, (uint32_t)editor->base_slot + 1, 0);
    }
}

static void response(ParametricScreen *editor, bool audition)
{
    SettingsEqualizerCurve curve = {.is_parametric = true, .parametric = editor->working};
    equalizer_response_make(&editor->response, &curve);
    sound_shaping_screen_texts(editor);
    if (audition && editor->config.preview != NULL) {
        editor->config.preview(editor->config.context, &curve);
        editor->previewing = true;
    }
}

void sound_shaping_screen_enter(ParametricScreen *editor)
{
    editor->shaping = (SoundShaping){.active = true};
    editor->base = (ParametricEq){0};
    editor->base_slot = -1;
    const ParametricBank *bank = editor->config.bank;
    if (editor->config.settings->equalizer == EQUALIZER_PARAMETRIC && bank != NULL &&
        bank->shaping[0].active && parametric_same(&bank->custom, &editor->working)) {
        editor->shaping = bank->shaping[0];
        editor->base = editor->working;
        editor->base.count = editor->shaping.base_count;
    }
    sound_shaping_build(&editor->working, &editor->base, &editor->shaping);
    editor->kept = editor->working;
    editor->kept_shaping = editor->shaping;
    editor->kept_base_slot = editor->base_slot;
    editor->notice = STRING_COUNT;
    response(editor, false);
}

static void restore(ParametricScreen *editor)
{
    editor->working = editor->kept;
    editor->shaping = editor->kept_shaping;
    editor->base = editor->kept;
    editor->base.count = editor->shaping.base_count;
    editor->base_slot = editor->kept_base_slot;
    editor->adjusting = false;
    response(editor, false);
    if (editor->previewing && editor->config.preview != NULL) {
        SettingsEqualizerCurve curve = settings_equalizer_curve(editor->config.settings);
        editor->config.preview(editor->config.context, &curve);
    }
    editor->previewing = false;
}

static void commit(ParametricScreen *editor, int slot)
{
    if (editor->config.shape_commit == NULL ||
        !editor->config.shape_commit(editor->config.context, &editor->working, &editor->shaping,
                                     slot)) {
        restore(editor);
        return;
    }
    editor->kept = editor->working;
    editor->kept_shaping = editor->shaping;
    editor->kept_base_slot = editor->base_slot;
    editor->adjusting = false;
    editor->previewing = false;
}

static void change_base(ParametricScreen *editor, int step)
{
    int slot = editor->base_slot + step;
    if (slot < -1 || slot >= PARAMETRIC_SLOT_COUNT) {
        return;
    }
    const ParametricBank *bank = editor->config.bank;
    if (slot >= 0 && (bank == NULL || (bank->used >> slot & 1) == 0)) {
        editor->base_slot = slot;
        editor->notice = STR_SHAPE_BASE_INVALID;
        sound_shaping_screen_texts(editor);
        return;
    }
    ParametricEq base = slot < 0 ? (ParametricEq){0} : bank->slots[slot];
    SoundShaping shape = editor->shaping;
    shape.base_count = base.count;
    ParametricEq next;
    if (!sound_shaping_build(&next, &base, &shape)) {
        editor->notice = STR_SHAPE_LIMIT;
        return;
    }
    editor->base_slot = slot;
    editor->base = base;
    editor->shaping = shape;
    editor->working = next;
    response(editor, true);
}

static void change(ParametricScreen *editor, int step)
{
    int row = editor->list.selected;
    if (row == BASE_ROW) {
        change_base(editor, step);
        return;
    }
    if (row == SAVE_ROW) {
        int slot = editor->slot + step;
        editor->slot = slot < 0                        ? 0
                       : slot >= PARAMETRIC_SLOT_COUNT ? PARAMETRIC_SLOT_COUNT - 1
                                                       : slot;
        sound_shaping_screen_texts(editor);
        return;
    }
    SoundShaping next = editor->shaping;
    int value = next.positions[row] + step;
    if (value < -SOUND_SHAPING_LIMIT || value > SOUND_SHAPING_LIMIT) {
        return;
    }
    next.positions[row] = (int8_t)value;
    if (!sound_shaping_build(&editor->working, &editor->base, &next)) {
        editor->notice = STR_SHAPE_LIMIT;
        return;
    }
    editor->shaping = next;
    response(editor, true);
}

static void select(ParametricScreen *editor)
{
    int row = editor->list.selected;
    if (editor->adjusting) {
        if (row == BASE_ROW && editor->notice != STRING_COUNT) {
            return;
        }
        commit(editor, row == SAVE_ROW ? editor->slot : -1);
    } else if (row == RESET_ROW) {
        for (int slider = 0; slider < SOUND_SHAPING_COUNT; slider++) {
            editor->shaping.positions[slider] = 0;
        }
        sound_shaping_build(&editor->working, &editor->base, &editor->shaping);
        response(editor, true);
        commit(editor, -1);
    } else {
        editor->adjusting = true;
    }
}

ScreenAction sound_shaping_screen_key(ParametricScreen *editor, NavigationIntent intent)
{
    StringId previous = editor->notice;
    editor->notice = STRING_COUNT;
    if (intent == NAVIGATION_NEXT || intent == NAVIGATION_PREVIOUS) {
        int step = intent == NAVIGATION_NEXT ? 1 : -1;
        if (editor->adjusting) {
            change(editor, step);
        } else {
            list_view_move(&editor->list, step, false, editor->now_ms);
        }
    } else if (intent == NAVIGATION_SELECT) {
        if (editor->adjusting && editor->list.selected == BASE_ROW && previous != STRING_COUNT) {
            editor->notice = previous;
        }
        select(editor);
    } else if (intent == NAVIGATION_BACK || intent == NAVIGATION_HOME) {
        bool adjusting = editor->adjusting;
        restore(editor);
        if (intent == NAVIGATION_HOME || !adjusting) {
            return intent == NAVIGATION_HOME ? screen_action_pop_to_root() : screen_action_pop();
        }
    }
    return screen_action_none();
}
