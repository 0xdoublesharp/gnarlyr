#include "formal.h"
#include "music_check.h"
#include "music_record.h"

void formal_version_fill(void);

void harness(void)
{
    MusicRecord record;
    formal_card_fill();
    formal_version_fill();
    (void)music_record_read(&record);
    (void)music_check_firmware_is_new();
    (void)music_check_covers_pending();
    __CPROVER_assert(music_check_interrupted_scans() <= 0xFF, "a scan count is one byte");
    (void)music_check_generation_moved();
    (void)music_record_settled();
}
