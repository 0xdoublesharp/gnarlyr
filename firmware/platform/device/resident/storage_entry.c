#include "storage_entry.h"

#include <stddef.h>

_Static_assert(sizeof(StorageCodeApi) == STORAGE_CODE_API_BYTES, "storage entry ABI size");
_Static_assert(offsetof(StorageCodeApi, order_open) == 12, "storage entry ABI layout");

const StorageCodeApi STORAGE_CODE_ENTRY = {
    .magic = STORAGE_CODE_API_MAGIC,
    .version = STORAGE_CODE_ABI,
    .size = sizeof(StorageCodeApi),
    .order_open = track_order_open,
    .order_open_at = track_order_open_at,
    .order_read = track_order_read,
    .order_source = track_order_source,
    .order_begin = track_order_job_begin,
    .order_step = track_order_job_step,
    .order_abort = track_order_job_abort,
    .order_progress = track_order_job_progress,
    .session_open = session_journal_open,
    .session_previous = session_journal_previous,
    .session_save = session_journal_save,
};
