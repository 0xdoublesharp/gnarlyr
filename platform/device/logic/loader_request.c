#include "device/loader_request.h"

static const uint8_t TEXT[LOADER_REQUEST_LENGTH] = {'M', 'A', 'S', 'K', 'R', 'O',
                                                    'M', ' ', 'R', 'E', 'Q'};

static bool holds_request(const LoaderRequestFile *file)
{
    uint8_t head[LOADER_REQUEST_LENGTH];
    if (file->size < LOADER_REQUEST_LENGTH || !file->read(file->context, 0, head, sizeof head)) {
        return false;
    }
    for (uint32_t index = 0; index < LOADER_REQUEST_LENGTH; index++) {
        if (head[index] != TEXT[index]) {
            return false;
        }
    }
    return true;
}

bool loader_request_consume(const LoaderRequestFile *file)
{
    static const uint8_t CLEARED[LOADER_REQUEST_LENGTH] = {0};
    if (!holds_request(file) || !file->write(file->context, 0, CLEARED, sizeof CLEARED)) {
        return false;
    }
    return !holds_request(file);
}
