#include "formal.h"
#include "theme_signature.h"

bool theme_signature_inspect(ByteSource source, uint32_t end, uint32_t length, bool *verified)
{
    (void)source;
    (void)end;
    (void)length;
    __CPROVER_assert(__CPROVER_w_ok(verified, sizeof *verified), "the verdict is writable");
    *verified = nondet_bool();
    return nondet_bool();
}
