#include "hal/storage.h"
#include "sd.h"

bool hal_card_eject(bool eject)
{
    if (!sd_present()) {
        return false;
    }
    sd_eject(eject);
    return true;
}
