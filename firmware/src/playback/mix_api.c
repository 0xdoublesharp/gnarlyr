#include "playback/mix_api.h"

static const MixApi API = {
    .catalog_open = mix_catalog_open,
    .catalog_previous = mix_catalog_previous,
    .catalog_read = mix_catalog_read,
    .catalog_begin = mix_catalog_begin,
    .catalog_step = mix_catalog_step,
    .catalog_abort = mix_catalog_abort,
    .identity_read = mix_identity_read,
    .identity_equal = mix_identity_equal,
    .identity_hash = mix_identity_hash,
    .measure_begin = mix_measure_begin,
    .measure_step = mix_measure_step,
    .tape_write_begin = mix_tape_write_begin,
    .tape_write_step = mix_tape_write_step,
    .tape_write_abort = mix_tape_write_abort,
    .tape_check_begin = mix_tape_check_begin,
    .tape_check_step = mix_tape_check_step,
    .tape_cursor = mix_tape_cursor,
    .tape_next = mix_tape_next,
    .resolve_begin = mix_resolve_begin,
    .resolve_step = mix_resolve_step,
    .resolve_source = mix_resolve_source,
};

const MixApi *mix_code_api(void)
{
    return &API;
}
