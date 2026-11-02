#include "device/update_names.h"

enum {
    ECHO_LOWEST = 100,
    ECHO_HIGHEST = 498,
    ECHO_COUNT = 1 + ECHO_HIGHEST - ECHO_LOWEST + 1
};

static const char ECHO_TEMPLATE[] = "/ECHOV000.IMG";
static const char ECHO_FIRST[] = "/ECHOECHO.IMG";

static void copy_text(char *name, const char *text, uint32_t size)
{
    for (uint32_t index = 0; index < size; index++) {
        name[index] = text[index];
    }
}

uint32_t update_name_count(void)
{
    return ECHO_COUNT;
}

static void put_digits(char *name, uint32_t at, uint32_t digits, uint32_t number)
{
    for (uint32_t index = digits; index > 0; index--) {
        name[at + index - 1] = (char)('0' + number % 10);
        number /= 10;
    }
}

bool update_name_at(uint32_t index, char name[UPDATE_NAME_CAPACITY])
{
    if (index >= update_name_count()) {
        return false;
    }
    if (index == 0) {
        copy_text(name, ECHO_FIRST, sizeof ECHO_FIRST);
    } else {
        copy_text(name, ECHO_TEMPLATE, sizeof ECHO_TEMPLATE);
        put_digits(name, 6, 3, ECHO_HIGHEST + 1 - index);
    }
    return true;
}
