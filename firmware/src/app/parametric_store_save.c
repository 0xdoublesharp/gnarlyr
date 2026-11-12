#include "parametric_store.h"

bool parametric_store_save(ParametricStore *store, const ParametricBank *bank)
{
    if (parametric_file_save(&store->file, bank) != STORE_DONE) {
        store->uncertain |= store->file.needs_reload;
        return false;
    }
    *parametric_store_bank() = *bank;
    store->description[0] = '\0';
    if (bank->shaping[0].active) {
        sound_shaping_words(&bank->shaping[0], store->description, sizeof store->description);
    }
    return true;
}
