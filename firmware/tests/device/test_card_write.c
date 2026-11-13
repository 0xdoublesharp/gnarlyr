#include "check.h"
#include "device/card_write.h"

typedef enum CallKind {
    CALL_DATA,
    CALL_STOP,
    CALL_READY,
    CALL_ERASE,
    CALL_BEGIN,
    CALL_MORE,
    CALL_END
} CallKind;

typedef struct Call {
    CallKind kind;
    uint8_t command;
    uint32_t address;
    uint32_t count;
    const uint32_t *words;
} Call;

typedef struct FakeCard {
    Call calls[8];
    int call_count;
    bool data_fails;
    bool end_fails;
    bool stop_fails;
    bool ready_fails;
    bool erase_fails;
    bool more_fails;
} FakeCard;

static void record(FakeCard *card, Call call)
{
    if (card->call_count < 8) {
        card->calls[card->call_count] = call;
    }
    card->call_count++;
}

static bool fake_write_data(void *context, uint8_t command, uint32_t address, uint32_t count,
                            const uint32_t *words)
{
    FakeCard *card = context;
    record(card, (Call){CALL_DATA, command, address, count, words});
    return !card->data_fails;
}

static bool fake_stop(void *context)
{
    FakeCard *card = context;
    record(card, (Call){CALL_STOP, 0, 0, 0, NULL});
    return !card->stop_fails;
}

static bool fake_wait_ready(void *context)
{
    FakeCard *card = context;
    record(card, (Call){CALL_READY, 0, 0, 0, NULL});
    return !card->ready_fails;
}

static bool fake_erase(void *context, uint32_t count)
{
    FakeCard *fake = context;
    record(fake, (Call){CALL_ERASE, 0, 0, count, NULL});
    return !fake->erase_fails;
}

static bool fake_begin(void *context, uint8_t command, uint32_t address, uint32_t count)
{
    FakeCard *fake = context;
    record(fake, (Call){CALL_BEGIN, command, address, count, NULL});
    return !fake->data_fails;
}

static bool fake_more(void *context, uint32_t *more, uint32_t count, uint32_t *accepted)
{
    FakeCard *fake = context;
    record(fake, (Call){CALL_MORE, 0, 0, count, more});
    *accepted = fake->more_fails ? 0 : count;
    return !fake->more_fails;
}

static bool fake_end(void *context)
{
    FakeCard *fake = context;
    record(fake, (Call){CALL_END, 0, 0, 0, NULL});
    return !fake->end_fails;
}

static FakeCard card;
static CardWriter writer;
static CardStreamOps ops;
static CardStream stream;
static uint32_t words[4 * 128];

static void start(void)
{
    card = (FakeCard){.call_count = 0};
    writer = (CardWriter){fake_write_data, fake_stop, fake_wait_ready, &card};
    ops = (CardStreamOps){fake_erase, fake_begin,      fake_more, fake_end,
                          fake_stop,  fake_wait_ready, &card,     false};
}

static void check_kinds(const CallKind *kinds, int count)
{
    CHECK_EQUAL(count, card.call_count);
    for (int index = 0; index < count && index < 8; index++) {
        CHECK_EQUAL(kinds[index], card.calls[index].kind);
    }
}

static void a_streamed_write_pre_erases_opens_once_and_stops_once_at_the_end(void)
{
    start();
    uint32_t accepted = 0;
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 4, false}));
    CHECK(card_stream_feed(&stream, words, 256, &accepted));
    CHECK_EQUAL(256, accepted);
    CHECK(card_stream_feed(&stream, words + 256, 256, &accepted));
    CHECK(card_stream_close(&stream, true));
    static const CallKind kinds[] = {CALL_ERASE, CALL_BEGIN, CALL_MORE, CALL_MORE,
                                     CALL_END,   CALL_STOP,  CALL_READY};
    check_kinds(kinds, 7);
    CHECK_EQUAL(4, card.calls[0].count);
    CHECK_EQUAL(CARD_WRITE_MULTIPLE_BLOCK, card.calls[1].command);
    CHECK_EQUAL(0x40, card.calls[1].address);
    CHECK_EQUAL(4, card.calls[1].count);
}

static void a_counted_stream_ends_without_a_stop(void)
{
    start();
    ops.counted = true;
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 4, false}));
    CHECK(card_stream_close(&stream, true));
    static const CallKind kinds[] = {CALL_ERASE, CALL_BEGIN, CALL_END, CALL_READY};
    check_kinds(kinds, 4);
}

static void a_count_the_card_refused_is_ended_with_a_stop(void)
{
    start();
    ops.counted = true;
    card.erase_fails = true;
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 4, false}));
    CHECK(card_stream_close(&stream, true));
    static const CallKind kinds[] = {CALL_ERASE, CALL_BEGIN, CALL_END, CALL_STOP, CALL_READY};
    check_kinds(kinds, 5);
}

static void a_counted_stream_that_breaks_is_still_stopped(void)
{
    start();
    ops.counted = true;
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 4, false}));
    CHECK(!card_stream_close(&stream, false));
    static const CallKind kinds[] = {CALL_ERASE, CALL_BEGIN, CALL_STOP, CALL_READY};
    check_kinds(kinds, 4);
}

static void a_one_block_stream_is_a_single_write_without_erase_or_stop(void)
{
    start();
    uint32_t accepted = 0;
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 1, false}));
    CHECK(card_stream_feed(&stream, words, 128, &accepted));
    CHECK(card_stream_close(&stream, true));
    static const CallKind kinds[] = {CALL_BEGIN, CALL_MORE, CALL_END, CALL_READY};
    check_kinds(kinds, 4);
    CHECK_EQUAL(CARD_WRITE_SINGLE_BLOCK, card.calls[0].command);
}

static void a_broken_stream_is_stopped_and_reported_as_failed(void)
{
    static const CallKind multiple[] = {CALL_ERASE, CALL_BEGIN, CALL_STOP, CALL_READY};
    start();
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 4, false}));
    CHECK(!card_stream_close(&stream, false));
    check_kinds(multiple, 4);
    start();
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 4, false}));
    card.end_fails = true;
    CHECK(!card_stream_close(&stream, true));
    static const CallKind ended[] = {CALL_ERASE, CALL_BEGIN, CALL_END, CALL_STOP, CALL_READY};
    check_kinds(ended, 5);
}

static void a_feed_failure_stops_further_feeding_and_fails_the_close(void)
{
    start();
    uint32_t accepted = 7;
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 4, false}));
    card.more_fails = true;
    CHECK(!card_stream_feed(&stream, words, 128, &accepted));
    CHECK_EQUAL(0, accepted);
    card.more_fails = false;
    CHECK(!card_stream_feed(&stream, words, 128, &accepted));
    CHECK(!card_stream_close(&stream, true));
    static const CallKind kinds[] = {CALL_ERASE, CALL_BEGIN, CALL_MORE, CALL_STOP, CALL_READY};
    check_kinds(kinds, 5);
}

static void a_streamed_read_is_one_cmd18_without_a_count_unless_counted(void)
{
    start();
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 4, true}));
    CHECK(card_stream_close(&stream, true));
    static const CallKind open_ended[] = {CALL_BEGIN, CALL_END, CALL_STOP, CALL_READY};
    check_kinds(open_ended, 4);
    CHECK_EQUAL(CARD_READ_MULTIPLE_BLOCK, card.calls[0].command);
    start();
    ops.counted = true;
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 4, true}));
    CHECK(card_stream_close(&stream, true));
    static const CallKind counted[] = {CALL_ERASE, CALL_BEGIN, CALL_END, CALL_READY};
    check_kinds(counted, 4);
    start();
    CHECK(card_stream_open(&stream, &ops, (CardStreamStart){0x40, 1, true}));
    CHECK_EQUAL(CARD_READ_SINGLE_BLOCK, card.calls[0].command);
}

static void a_refused_open_fails_and_is_stopped_on_close(void)
{
    start();
    card.data_fails = true;
    CHECK(!card_stream_open(&stream, &ops, (CardStreamStart){0x40, 1, false}));
    CHECK(!card_stream_close(&stream, true));
    static const CallKind kinds[] = {CALL_BEGIN, CALL_STOP, CALL_READY};
    check_kinds(kinds, 3);
}

static void one_block_is_a_single_write_then_a_ready_wait(void)
{
    start();
    CHECK(card_write(&writer, 0x1234, 1, words));
    CHECK_EQUAL(2, card.call_count);
    CHECK_EQUAL(CALL_DATA, card.calls[0].kind);
    CHECK_EQUAL(CARD_WRITE_SINGLE_BLOCK, card.calls[0].command);
    CHECK_EQUAL(0x1234, card.calls[0].address);
    CHECK_EQUAL(1, card.calls[0].count);
    CHECK(card.calls[0].words == words);
    CHECK_EQUAL(CALL_READY, card.calls[1].kind);
}

static void several_blocks_are_one_multiple_write_a_stop_and_a_ready_wait(void)
{
    start();
    CHECK(card_write(&writer, 0x40, 4, words));
    CHECK_EQUAL(3, card.call_count);
    CHECK_EQUAL(CALL_DATA, card.calls[0].kind);
    CHECK_EQUAL(CARD_WRITE_MULTIPLE_BLOCK, card.calls[0].command);
    CHECK_EQUAL(0x40, card.calls[0].address);
    CHECK_EQUAL(4, card.calls[0].count);
    CHECK(card.calls[0].words == words);
    CHECK_EQUAL(CALL_STOP, card.calls[1].kind);
    CHECK_EQUAL(CALL_READY, card.calls[2].kind);
}

static void a_failed_multiple_write_still_stops_and_waits_but_fails(void)
{
    start();
    card.data_fails = true;
    CHECK(!card_write(&writer, 0x40, 4, words));
    CHECK_EQUAL(3, card.call_count);
    CHECK_EQUAL(CALL_STOP, card.calls[1].kind);
    CHECK_EQUAL(CALL_READY, card.calls[2].kind);
}

static void a_failed_stop_skips_the_ready_wait_and_fails(void)
{
    start();
    card.stop_fails = true;
    CHECK(!card_write(&writer, 0x40, 4, words));
    CHECK_EQUAL(2, card.call_count);
    CHECK_EQUAL(CALL_STOP, card.calls[1].kind);
}

static void a_card_that_never_becomes_ready_fails_the_write(void)
{
    start();
    card.ready_fails = true;
    CHECK(!card_write(&writer, 0x40, 4, words));
    start();
    card.ready_fails = true;
    CHECK(!card_write(&writer, 0x40, 1, words));
}

static void a_failed_single_write_does_not_wait(void)
{
    start();
    card.data_fails = true;
    CHECK(!card_write(&writer, 0x40, 1, words));
    CHECK_EQUAL(1, card.call_count);
}

static void no_blocks_send_nothing(void)
{
    start();
    CHECK(card_write(&writer, 0x40, 0, words));
    CHECK_EQUAL(0, card.call_count);
}

static void starting_one_block_only_sends_it(void)
{
    start();
    CHECK(card_write_start(&writer, 0x40, 1, words));
    CHECK_EQUAL(1, card.call_count);
    CHECK_EQUAL(CALL_DATA, card.calls[0].kind);
    CHECK_EQUAL(CARD_WRITE_SINGLE_BLOCK, card.calls[0].command);
}

static void starting_several_blocks_sends_them_and_the_stop_without_waiting(void)
{
    start();
    CHECK(card_write_start(&writer, 0x40, 4, words));
    CHECK_EQUAL(2, card.call_count);
    CHECK_EQUAL(CARD_WRITE_MULTIPLE_BLOCK, card.calls[0].command);
    CHECK_EQUAL(CALL_STOP, card.calls[1].kind);
}

static void finishing_waits_for_the_card(void)
{
    start();
    CHECK(card_write_finish(&writer));
    CHECK_EQUAL(1, card.call_count);
    CHECK_EQUAL(CALL_READY, card.calls[0].kind);
    start();
    card.ready_fails = true;
    CHECK(!card_write_finish(&writer));
}

static void a_failed_start_brings_the_card_back_at_once(void)
{
    start();
    card.data_fails = true;
    CHECK(!card_write_start(&writer, 0x40, 4, words));
    CHECK_EQUAL(3, card.call_count);
    CHECK_EQUAL(CALL_READY, card.calls[2].kind);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_streamed_write_pre_erases_opens_once_and_stops_once_at_the_end),
        TEST_CASE(a_counted_stream_ends_without_a_stop),
        TEST_CASE(a_count_the_card_refused_is_ended_with_a_stop),
        TEST_CASE(a_counted_stream_that_breaks_is_still_stopped),
        TEST_CASE(a_one_block_stream_is_a_single_write_without_erase_or_stop),
        TEST_CASE(a_broken_stream_is_stopped_and_reported_as_failed),
        TEST_CASE(a_feed_failure_stops_further_feeding_and_fails_the_close),
        TEST_CASE(a_refused_open_fails_and_is_stopped_on_close),
        TEST_CASE(a_streamed_read_is_one_cmd18_without_a_count_unless_counted),
        TEST_CASE(one_block_is_a_single_write_then_a_ready_wait),
        TEST_CASE(several_blocks_are_one_multiple_write_a_stop_and_a_ready_wait),
        TEST_CASE(a_failed_multiple_write_still_stops_and_waits_but_fails),
        TEST_CASE(a_failed_stop_skips_the_ready_wait_and_fails),
        TEST_CASE(a_card_that_never_becomes_ready_fails_the_write),
        TEST_CASE(a_failed_single_write_does_not_wait),
        TEST_CASE(no_blocks_send_nothing),
        TEST_CASE(starting_one_block_only_sends_it),
        TEST_CASE(starting_several_blocks_sends_them_and_the_stop_without_waiting),
        TEST_CASE(finishing_waits_for_the_card),
        TEST_CASE(a_failed_start_brings_the_card_back_at_once),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
