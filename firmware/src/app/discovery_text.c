#include "discovery_text.h"

#include "core/string_builder.h"
#include "gfx/text.h"

static const char LETTERS[] = " abcdefghijklmnopqrstuvwxyz0123456789-_";
static const char DIGITS[] = "0123456789";

static const char *characters(const DiscoveryText *editor)
{
    return editor->numeric ? DIGITS : LETTERS;
}

static unsigned text_length(const char *text)
{
    unsigned length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

void discovery_text_begin(DiscoveryText *editor, const char *text, bool numeric)
{
    *editor = (DiscoveryText){.character = numeric ? 0 : 1, .numeric = numeric};
    StringBuilder builder = string_builder_make(editor->text, sizeof editor->text);
    string_builder_append(&builder, text);
}

static void erase(DiscoveryText *editor)
{
    unsigned length = text_length(editor->text);
    if (length != 0) {
        do {
            length--;
        } while (length != 0 && ((uint8_t)editor->text[length] & 0xc0u) == 0x80u);
        editor->text[length] = 0;
    }
}

void discovery_text_key(DiscoveryText *editor, KeyEvent event)
{
    const char *choices = characters(editor);
    int count = (int)text_length(choices);
    if (event.key == KEY_VOLUME_UP || event.key == KEY_VOLUME_DOWN) {
        int direction = event.key == KEY_VOLUME_UP ? 1 : -1;
        editor->character = (uint8_t)((editor->character + direction + count) % count);
    } else if (event.key == KEY_MENU && event.gesture == KEY_GESTURE_PRESS) {
        erase(editor);
    } else if (event.key == KEY_PLAY && event.gesture == KEY_GESTURE_PRESS) {
        unsigned length = text_length(editor->text);
        if (length + 1 < sizeof editor->text) {
            editor->text[length] = choices[editor->character];
            editor->text[length + 1] = 0;
        }
    }
}

char discovery_text_character(const DiscoveryText *editor)
{
    return characters(editor)[editor->character];
}

const char *discovery_text_tail(const char *text, const Font *font, int width)
{
    while (*text != 0 && text_width(font, text) > width) {
        do {
            text++;
        } while (((uint8_t)*text & 0xc0u) == 0x80u);
    }
    return text;
}

bool discovery_text_number(const DiscoveryText *editor, uint32_t limit, uint32_t *value)
{
    uint32_t number = 0;
    for (unsigned index = 0; editor->text[index] != 0; index++) {
        char digit = editor->text[index];
        if (digit < '0' || digit > '9' || (uint32_t)(digit - '0') > limit ||
            number > (limit - (uint32_t)(digit - '0')) / 10) {
            return false;
        }
        number = number * 10 + (uint32_t)(digit - '0');
    }
    *value = number;
    return true;
}
