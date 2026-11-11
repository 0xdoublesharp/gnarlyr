#include "device/opus_decode.h"

#include "opus.h"
#include "opus_support.h"

static OpusDecode *active;

static bool range_valid(const void *pointer, size_t bytes)
{
    return pointer != NULL && bytes <= UINTPTR_MAX - (uintptr_t)pointer;
}

static bool overlap(const void *first, size_t first_bytes, const void *second, size_t second_bytes)
{
    return (uintptr_t)first < (uintptr_t)second + second_bytes &&
           (uintptr_t)second < (uintptr_t)first + first_bytes;
}

static _Noreturn void leave(OpusDecodeResult error)
{
    if (active == NULL) {
        __builtin_trap();
    }
    active->error = error;
#if defined(__arm__) && defined(__GNUC__)
    __builtin_longjmp(active->checkpoint, 1);
#else
    longjmp(active->checkpoint, 1);
#endif
}

void *opus_decode_allocate(size_t count, size_t bytes, size_t alignment)
{
    if (active == NULL) {
        __builtin_trap();
    }
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 || bytes == 0 ||
        count > SIZE_MAX / bytes) {
        leave(OPUS_DECODE_EXHAUSTED);
    }
    size_t padding = (0u - ((uintptr_t)active->scratch + active->scratch_used)) & (alignment - 1);
    size_t available = active->scratch_bytes - active->scratch_used;
    size_t length = count * bytes;
    if (padding > available || length > available - padding) {
        leave(OPUS_DECODE_EXHAUSTED);
    }
    void *block = active->scratch + active->scratch_used + padding;
    active->scratch_used += (uint32_t)(padding + length);
    if (active->scratch_used > active->scratch_peak) {
        active->scratch_peak = active->scratch_used;
    }
    return block;
}

void *opus_decode_mark(void)
{
    if (active == NULL) {
        __builtin_trap();
    }
    return active->scratch + active->scratch_used;
}

void opus_decode_restore(void *mark)
{
    if (active == NULL) {
        __builtin_trap();
    }
    uintptr_t address = (uintptr_t)mark;
    if (address < (uintptr_t)active->scratch ||
        address - (uintptr_t)active->scratch > active->scratch_used) {
        leave(OPUS_DECODE_INVALID);
    }
    active->scratch_used = (uint32_t)(address - (uintptr_t)active->scratch);
}

void *opus_alloc(size_t bytes)
{
    (void)bytes;
    leave(OPUS_DECODE_EXHAUSTED);
}

void *opus_realloc(void *pointer, size_t bytes)
{
    (void)pointer;
    (void)bytes;
    leave(OPUS_DECODE_EXHAUSTED);
}

void opus_free(void *pointer)
{
    (void)pointer;
}

void *opus_alloc_scratch(size_t bytes)
{
    return opus_decode_allocate(bytes, 1, 8);
}

_Noreturn void celt_fatal(const char *message, const char *file, int line)
{
    (void)message;
    (void)file;
    (void)line;
    leave(OPUS_DECODE_INVALID);
}

uint32_t opus_decode_state_bytes(uint32_t channels)
{
    return channels >= 1 && channels <= 2 ? (uint32_t)opus_decoder_get_size((int)channels) : 0;
}

static OpusDecodeResult release(OpusDecode *decoder, OpusDecodeResult error)
{
    decoder->scratch_used = 0;
    decoder->error = error;
    if (error != OPUS_DECODE_OK) {
        decoder->ready = false;
    }
    active = NULL;
    return error;
}

#if defined(__arm__) && defined(__GNUC__)
#define OPUS_DECODE_TRY(decoder) __builtin_setjmp((decoder)->checkpoint)
#else
#define OPUS_DECODE_TRY(decoder) setjmp((decoder)->checkpoint)
#endif

OpusDecodeResult opus_decode_init(OpusDecode *decoder, void *state, uint32_t state_bytes,
                                  void *scratch, uint32_t scratch_bytes, uint32_t rate,
                                  uint32_t channels)
{
    if (active != NULL) {
        return OPUS_DECODE_BUSY;
    }
    if (decoder == NULL) {
        return OPUS_DECODE_INVALID;
    }
    if ((range_valid(state, state_bytes) &&
         overlap(decoder, sizeof *decoder, state, state_bytes)) ||
        (range_valid(scratch, scratch_bytes) &&
         overlap(decoder, sizeof *decoder, scratch, scratch_bytes))) {
        return OPUS_DECODE_INVALID;
    }
    *decoder = (OpusDecode){0};
    uint32_t needed = opus_decode_state_bytes(channels);
    if (needed == 0 || state_bytes < needed || scratch_bytes < 8 ||
        !range_valid(state, state_bytes) || !range_valid(scratch, scratch_bytes) ||
        (uintptr_t)state % 8 != 0 || (uintptr_t)scratch % 8 != 0 ||
        overlap(state, state_bytes, scratch, scratch_bytes) ||
        overlap(decoder, sizeof *decoder, state, state_bytes) ||
        overlap(decoder, sizeof *decoder, scratch, scratch_bytes) ||
        (rate != 8000 && rate != 12000 && rate != 16000 && rate != 24000 && rate != 48000)) {
        return OPUS_DECODE_INVALID;
    }
    *decoder = (OpusDecode){.state = state,
                            .scratch = scratch,
                            .state_bytes = needed,
                            .scratch_bytes = scratch_bytes,
                            .rate = rate,
                            .channels = channels};
    active = decoder;
    if (OPUS_DECODE_TRY(decoder) != 0) {
        return release(decoder, decoder->error);
    }
    if (opus_decoder_init(state, (opus_int32)rate, (int)channels) != OPUS_OK) {
        return release(decoder, OPUS_DECODE_INVALID);
    }
    decoder->ready = true;
    return release(decoder, OPUS_DECODE_OK);
}

static bool touches_decoder(const OpusDecode *decoder, const void *pointer, uint32_t bytes)
{
    return overlap(pointer, bytes, decoder->state, decoder->state_bytes) ||
           overlap(pointer, bytes, decoder->scratch, decoder->scratch_bytes) ||
           overlap(pointer, bytes, decoder, sizeof *decoder);
}

OpusDecodeResult opus_decode_packet(OpusDecode *decoder, OpusDecodeInput input, OpusDecodePcm *pcm)
{
    if (active != NULL) {
        return OPUS_DECODE_BUSY;
    }
    if (decoder == NULL || pcm == NULL) {
        return OPUS_DECODE_INVALID;
    }
    if (!range_valid(pcm, sizeof *pcm) || touches_decoder(decoder, pcm, sizeof *pcm) ||
        (input.packet != NULL && range_valid(input.packet, input.bytes) &&
         overlap(pcm, sizeof *pcm, input.packet, input.bytes))) {
        return OPUS_DECODE_INVALID;
    }
    uint32_t possible =
        pcm->capacity < OPUS_DECODE_MAX_FRAMES ? pcm->capacity : OPUS_DECODE_MAX_FRAMES;
    size_t possible_bytes = (size_t)possible * decoder->channels * sizeof(int16_t);
    if (range_valid(pcm->samples, possible_bytes) &&
        overlap(pcm, sizeof *pcm, pcm->samples, possible_bytes)) {
        return OPUS_DECODE_INVALID;
    }
    pcm->frames = 0;
    if (!decoder->ready) {
        return OPUS_DECODE_FAILED;
    }
    uint32_t maximum = decoder->rate * 3 / 25;
    bool lost = input.packet == NULL;
    if ((lost && (input.bytes != 0 || input.fec)) ||
        (!lost && (input.bytes == 0 || input.bytes > OPUS_DECODE_MAX_PACKET_BYTES ||
                   !range_valid(input.packet, input.bytes) ||
                   touches_decoder(decoder, input.packet, input.bytes))) ||
        ((lost || input.fec) && (input.frames == 0 || input.frames > maximum ||
                                 input.frames % (decoder->rate / 400) != 0)) ||
        (!lost && !input.fec && input.frames != 0)) {
        return OPUS_DECODE_INVALID;
    }
    int frames = lost || input.fec
                     ? (int)input.frames
                     : opus_packet_get_nb_samples(input.packet, (opus_int32)input.bytes,
                                                  (opus_int32)decoder->rate);
    if (frames <= 0 || (uint32_t)frames > maximum) {
        return OPUS_DECODE_INVALID;
    }
    uint32_t output_bytes = (uint32_t)frames * decoder->channels * sizeof(int16_t);
    if (!range_valid(pcm->samples, output_bytes) || (uintptr_t)pcm->samples % 2 != 0 ||
        touches_decoder(decoder, pcm->samples, output_bytes) ||
        (!lost && overlap(input.packet, input.bytes, pcm->samples, output_bytes))) {
        return OPUS_DECODE_INVALID;
    }
    if (pcm->capacity < (uint32_t)frames) {
        return OPUS_DECODE_BUFFER_SMALL;
    }
    OpusDecodeMode mode = lost                               ? decoder->mode
                          : (input.packet[0] & 0x80) != 0    ? OPUS_DECODE_CELT
                          : (input.packet[0] & 0x60) == 0x60 ? OPUS_DECODE_HYBRID
                                                             : OPUS_DECODE_SILK;
    active = decoder;
    if (OPUS_DECODE_TRY(decoder) != 0) {
        return release(decoder, decoder->error);
    }
    int result = opus_decode(decoder->state, input.packet, (opus_int32)input.bytes, pcm->samples,
                             frames, input.fec ? 1 : 0);
    if (result <= 0 || result > frames) {
        return release(decoder, OPUS_DECODE_INVALID);
    }
    decoder->mode = mode;
    pcm->frames = (uint32_t)result;
    pcm->rate = decoder->rate;
    pcm->channels = decoder->channels;
    pcm->mode = mode;
    return release(decoder, OPUS_DECODE_OK);
}
