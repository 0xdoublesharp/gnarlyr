#include "core/i18n.h"
#include "formal.h"

static bool attached;
static char code_text[I18N_CODE_BYTES];
static uint32_t loaded_segment;

bool i18n_pack_info(const uint8_t *index, size_t length, I18nPackInfo *info)
{
    __CPROVER_assert(__CPROVER_r_ok(index, length), "a pack index is readable");
    if (index == NULL || length < I18N_INDEX_BYTES || nondet_bool()) {
        return false;
    }
    __CPROVER_havoc_object(info);
    __CPROVER_assume(info->resident <= I18N_INDEX_BYTES + 3u * I18N_FILE_CAPACITY);
    info->code[I18N_CODE_BYTES - 1] = '\0';
    info->name[I18N_NAME_BYTES - 1] = '\0';
    return true;
}

bool i18n_pack_segment(const uint8_t *index, uint32_t segment, I18nSegment *row)
{
    __CPROVER_assert(__CPROVER_r_ok(index, I18N_INDEX_BYTES), "a pack index is readable");
    __CPROVER_assert(segment < STRING_SEGMENT_COUNT, "the segment exists");
    __CPROVER_havoc_object(row);
    return nondet_bool();
}

bool i18n_attach(const uint8_t *pack, size_t length)
{
    __CPROVER_assert(__CPROVER_r_ok(pack, length), "an attached pack is readable");
    attached = nondet_bool();
    __CPROVER_havoc_object(code_text);
    code_text[I18N_CODE_BYTES - 1] = '\0';
    return attached;
}

bool i18n_attach_segment(uint32_t segment, const uint8_t *block, const I18nSegment *row)
{
    __CPROVER_assert(__CPROVER_r_ok(block, row->size), "a module segment is readable");
    loaded_segment = nondet_bool() ? segment : 0;
    return loaded_segment != 0;
}

void i18n_detach_segment(void)
{
    loaded_segment = 0;
}

uint32_t i18n_segment_loaded(void)
{
    return loaded_segment;
}

void i18n_detach(void)
{
    attached = false;
    loaded_segment = 0;
}

bool i18n_attached(void)
{
    return attached;
}

const char *i18n_code(void)
{
    return attached ? code_text : "";
}
