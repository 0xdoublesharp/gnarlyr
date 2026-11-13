#include "device/card_write.h"

bool card_write_start(const CardWriter *writer, uint32_t address, uint32_t count,
                      const uint32_t *words)
{
    if (count == 0) {
        return true;
    }
    if (count == 1) {
        return writer->write_data(writer->context, CARD_WRITE_SINGLE_BLOCK, address, 1, words);
    }
    bool written =
        writer->write_data(writer->context, CARD_WRITE_MULTIPLE_BLOCK, address, count, words);
    if (!writer->stop(writer->context)) {
        return false;
    }
    if (!written) {
        writer->wait_ready(writer->context);
    }
    return written;
}

bool card_write_finish(const CardWriter *writer)
{
    return writer->wait_ready(writer->context);
}

bool card_write(const CardWriter *writer, uint32_t address, uint32_t count, const uint32_t *words)
{
    if (count == 0) {
        return true;
    }
    return card_write_start(writer, address, count, words) && card_write_finish(writer);
}

bool card_stream_open(CardStream *stream, const CardStreamOps *ops, uint32_t address,
                      uint32_t count)
{
    bool counted = count > 1 && ops->pre_count(ops->context, count);
    uint8_t command = count == 1 ? CARD_WRITE_SINGLE_BLOCK : CARD_WRITE_MULTIPLE_BLOCK;
    *stream = (CardStream){ops, count, counted && ops->counted, false};
    stream->failed = !ops->begin(ops->context, command, address, count);
    return !stream->failed;
}

bool card_stream_feed(CardStream *stream, const uint32_t *words, uint32_t count, uint32_t *accepted)
{
    *accepted = 0;
    if (!stream->failed) {
        stream->failed = !stream->ops->feed(stream->ops->context, words, count, accepted);
    }
    return !stream->failed;
}

bool card_stream_close(CardStream *stream, bool complete)
{
    const CardStreamOps *ops = stream->ops;
    bool ended = complete && !stream->failed && ops->end(ops->context);
    bool stop = !ended || (stream->count > 1 && !stream->closed_ended);
    bool stopped = !stop || ops->stop(ops->context);
    bool ready = ops->wait_ready(ops->context);
    return ended && stopped && ready;
}
