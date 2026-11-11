#include "library/sort_key.h"

char sort_key_initial(const char *text)
{
    char key[SORT_KEY_CAPACITY];
    sort_key_make(text, key, sizeof key);
    return key[0];
}
