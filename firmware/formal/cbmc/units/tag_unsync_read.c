#include "formal.h"
#include "tags_picture.c"

#ifndef FORMAL_UNSYNC_BUFFER
#define FORMAL_UNSYNC_BUFFER 128
#endif

void harness(void)
{
    uint8_t memory[FORMAL_UNSYNC_BUFFER];
    uint32_t total = FORMAL_UNSYNC_BUFFER;
    uint32_t start = formal_below(total + 1);
    uint32_t length = formal_below(total - start + 1);
    uint32_t witness = formal_below(total);
    __CPROVER_assume(witness < start || witness >= start + length);
    uint8_t before = memory[witness];
    formal_card_fill();
    TagUnsync view;
    tag_unsync_start(&view, formal_card_source(), nondet_uint32(), nondet_uint32());
    view.raw = nondet_uint32();
    view.decoded = nondet_uint32();
    view.after_ff = nondet_bool();
    (void)tag_unsync_read(&view, nondet_uint32(), memory + start, length);
    __CPROVER_assert(view.source.read == formal_card_read && view.source.context == NULL,
                     "the view keeps its source");
    __CPROVER_assert(memory[witness] == before, "nothing outside the buffer is written");
}
