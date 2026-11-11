#include "parametric_store.h"

bool parametric_store_save(ParametricStore *store, const ParametricBank *bank)
{
    if (parametric_file_save(&store->file, bank) != STORE_DONE) {
        store->uncertain |= store->file.needs_reload;
        return false;
    }
    *parametric_store_bank() = *bank;
    return true;
}
