#include "listening_store.h"

StoreResult listening_store_clear(ListeningStore *store)
{
    StoreResult result = listening_log_clear(&store->log);
    if (result == STORE_DONE || result == STORE_UNCERTAIN) {
        store->active = (ListeningPlay){0};
        store->pending = (ListeningPlay){0};
        store->following = false;
        store->waiting = false;
        store->observed = false;
        store->failed = result != STORE_DONE;
    }
    return result;
}
