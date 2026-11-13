#include "core/debug_script.h"

enum {
    CASE_BIT = 0x20
};

static char lowered(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character | CASE_BIT) : character;
}

bool debug_span_matches(DebugSpan span, const char *text)
{
    size_t index = 0;
    while (index < span.length && text[index] != '\0' &&
           lowered(span.text[index]) == lowered(text[index])) {
        index++;
    }
    return index == span.length && text[index] == '\0';
}
