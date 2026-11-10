#include "device/mp4_demux.h"

bool mp4_signature(const uint8_t bytes[8])
{
    return bytes[4] == 'f' && bytes[5] == 't' && bytes[6] == 'y' && bytes[7] == 'p';
}
