#include "device/jack_sense.h"

bool jack_sense_sample(JackSense *sense, JackKind reading)
{
    if (!sense->started) {
        *sense = (JackSense){.started = true, .stable = reading, .candidate = reading};
        return true;
    }
    if (reading == sense->stable) {
        sense->matches = 0;
        return false;
    }
    if (reading != sense->candidate) {
        sense->candidate = reading;
        sense->matches = 0;
    }
    sense->matches++;
    if (sense->matches < JACK_SENSE_SAMPLES) {
        return false;
    }
    sense->stable = reading;
    sense->matches = 0;
    return true;
}
