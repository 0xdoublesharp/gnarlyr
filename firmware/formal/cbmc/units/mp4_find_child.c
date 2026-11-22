#include "formal.h"
#include "tags_internal.h"

void harness(void)
{
    (void)tags_mp4_find_child(NULL, NULL, nondet_bool() ? 4 : 0, NULL, NULL);
}
