#include "check.h"
#include "device/card_presence.h"
#include "device/mmc_clock.h"
#include "device/sd_card.h"

enum {
    LOG_CAPACITY = 256,
    BUSY_TRIES = 3
};

typedef struct FakeCard {
    bool present;
    bool answers_cmd8;
    uint32_t cmd8_echo;
    bool high_capacity;
    int busy_tries;
    int zero_rca_answers;
    uint32_t csd[4];
    bool wide_bus;
    uint8_t switches;
    uint8_t fault;
    int log_count;
    SdCommand log[LOG_CAPACITY];
    SdClock clock;
    uint8_t bus_width;
    uint32_t delays_ms;
    bool stopped;
    bool silent;
} FakeCard;

static bool answer(FakeCard *card, SdCommand command, uint32_t response[4])
{
    switch (command.index) {
    case 8:
        response[0] = card->cmd8_echo != 0 ? card->cmd8_echo : command.argument & 0xFFF;
        return card->answers_cmd8;
    case 55:
        return !card->silent;
    case 41:
        if (card->silent) {
            return false;
        }
        if (card->busy_tries > 0) {
            card->busy_tries--;
            response[0] = 0x00FF8000;
        } else {
            response[0] = 0x80FF8000 | (card->high_capacity ? 0x40000000u : 0);
        }
        return true;
    case 3:
        response[0] = card->zero_rca_answers-- > 0 ? 0 : 0x12340500;
        return true;
    case 9:
        for (int index = 0; index < 4; index++) {
            response[index] = card->csd[index];
        }
        return true;
    case 6:
        return card->wide_bus;
    case 13:
        response[0] = card->fault == 5 ? 0x00400900 : 0x900;
        return true;
    default:
        response[0] = 0x900;
        return true;
    }
}

static bool fake_command(void *context, SdCommand command, uint32_t response[4])
{
    FakeCard *card = context;
    if (card->log_count < LOG_CAPACITY) {
        card->log[card->log_count++] = command;
    }
    return card->present && answer(card, command, response);
}

static bool fake_switch(void *context, uint32_t argument, uint32_t status[16])
{
    FakeCard *card = context;
    CHECK_EQUAL(card->switches++ ? 0x80FFFFF1u : 0x00FFFFF1u, argument);
    uint8_t *bytes = (uint8_t *)status;
    for (int word = 0; word < 16; word++) {
        status[word] = 0;
    }
    bytes[13] = card->fault == 1 ? 0 : 2;
    bytes[16] = card->fault == 2 && card->switches == 2 ? 0xF : 1;
    bytes[17] = 1;
    bytes[29] = card->fault == 3 ? 2 : 0;
    return card->fault != 4 || card->switches == 1;
}

static bool fake_set_clock(void *context, SdClock clock)
{
    ((FakeCard *)context)->clock = clock;
    return true;
}

static void fake_set_bus_width(void *context, uint8_t bits)
{
    ((FakeCard *)context)->bus_width = bits;
}

static void fake_delay(void *context, uint32_t milliseconds)
{
    ((FakeCard *)context)->delays_ms += milliseconds;
}

static bool fake_present(void *context)
{
    return ((FakeCard *)context)->present;
}

static void fake_stop(void *context)
{
    ((FakeCard *)context)->stopped = true;
}

static SdHost host_for(FakeCard *card)
{
    return (SdHost){fake_command, fake_set_clock, fake_set_bus_width, fake_delay, fake_present,
                    fake_stop,    card,           fake_switch};
}

static void set_bits(uint32_t csd[4], int start, int size, uint32_t value)
{
    for (int bit = 0; bit < size; bit++) {
        int position = start + bit;
        if ((value >> bit) & 1u) {
            csd[position / 32] |= 1u << (position % 32);
        }
    }
}

static FakeCard sdhc_card(uint32_t c_size)
{
    FakeCard card = {.present = true,
                     .answers_cmd8 = true,
                     .high_capacity = true,
                     .busy_tries = BUSY_TRIES,
                     .wide_bus = true};
    set_bits(card.csd, 126, 2, 1);
    set_bits(card.csd, 48, 22, c_size);
    return card;
}

static FakeCard sdsc_v1_card(void)
{
    FakeCard card = {.present = true, .answers_cmd8 = false, .busy_tries = 1, .wide_bus = true};
    set_bits(card.csd, 80, 4, 9);
    set_bits(card.csd, 62, 12, 4095);
    set_bits(card.csd, 47, 3, 7);
    return card;
}

static const SdCommand *find_command(const FakeCard *card, uint8_t index, int occurrence)
{
    for (int position = 0; position < card->log_count; position++) {
        if (card->log[position].index == index && occurrence-- == 0) {
            return &card->log[position];
        }
    }
    return NULL;
}

static void an_sdhc_card_initialises_with_its_size_and_a_4_bit_bus(void)
{
    FakeCard card = sdhc_card(0x00EDC7);
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(sd_card_init(&sd, &host));
    CHECK(sd.high_capacity);
    CHECK_EQUAL(0x1234, sd.rca);
    CHECK_EQUAL((0x00EDC7 + 1) * 1024, sd.sector_count);
    CHECK_EQUAL(4, sd.bus_width);
    CHECK_EQUAL(4, card.bus_width);
    CHECK_EQUAL(SD_CLOCK_TRANSFER, card.clock);
}

static void high_speed_requires_both_a_supported_check_and_a_successful_switch(void)
{
    FakeCard card = sdhc_card(100);
    set_bits(card.csd, 94, 1, 1);
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(sd_card_init(&sd, &host));
    CHECK(sd.high_speed);
    CHECK_EQUAL(2, card.switches);
    for (uint8_t fault = 1; fault <= 5; fault++) {
        card.switches = 0;
        card.fault = fault;
        CHECK(sd_card_init(&sd, &host));
        CHECK(!sd.high_speed);
        CHECK_EQUAL(fault == 1 || fault == 3 ? 1 : 2, card.switches);
        CHECK_EQUAL(SD_CLOCK_TRANSFER, card.clock);
    }
}

static void mmc_dividers_keep_each_speed_at_or_below_its_limits(void)
{
    CHECK_EQUAL(3, mmc_clock_field(200, 52));
    CHECK_EQUAL(3, mmc_clock_field(200, 50));
    CHECK_EQUAL(1, mmc_clock_field(96, 52));
    CHECK_EQUAL(1, mmc_clock_field(96, 50));
    CHECK_EQUAL(7, mmc_clock_field(200, 26));
    CHECK_EQUAL(MMC_CRYSTAL_SOURCE, mmc_clock_field(96, 26));
    CHECK_EQUAL(MMC_CRYSTAL_SOURCE, mmc_clock_field(24, 52));
    CHECK_EQUAL(MMC_CRYSTAL_SOURCE, mmc_clock_field(200, 0));
    CHECK_EQUAL(MMC_CRYSTAL_SOURCE, mmc_clock_field(201, 50));
}

static void emmc_requires_a_supported_speed_and_clean_ready_transfer_status(void)
{
    CHECK_EQUAL(0, mmc_emmc_limit_mhz(0xFC));
    CHECK_EQUAL(26, mmc_emmc_limit_mhz(1));
    CHECK_EQUAL(52, mmc_emmc_limit_mhz(2));
    CHECK_EQUAL(52, mmc_emmc_limit_mhz(3));
    CHECK(mmc_transfer_ready(0x900));
    CHECK(!mmc_transfer_ready(0x800));
    CHECK(!mmc_transfer_ready(0xB00));
    CHECK(!mmc_transfer_ready(0x980));
    CHECK(!mmc_transfer_ready(0x00400900));
}

static void the_commands_follow_the_sd_init_order(void)
{
    static const uint8_t expected[] = {0,  8, 55, 41, 55, 41, 55, 41, 55,
                                       41, 2, 3,  9,  7,  55, 42, 55, 6};
    FakeCard card = sdhc_card(100);
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(sd_card_init(&sd, &host));
    CHECK_EQUAL(sizeof expected, card.log_count);
    for (int index = 0; index < card.log_count && index < (int)sizeof expected; index++) {
        CHECK_EQUAL(expected[index], card.log[index].index);
    }
}

static void command_arguments_and_responses_match_the_facts_sheet(void)
{
    FakeCard card = sdhc_card(100);
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(sd_card_init(&sd, &host));
    CHECK(find_command(&card, 0, 0)->initialise);
    CHECK_EQUAL(SD_RESPONSE_NONE, find_command(&card, 0, 0)->response);
    CHECK_EQUAL(0x1AA, find_command(&card, 8, 0)->argument);
    CHECK_EQUAL(0x40FF8000, find_command(&card, 41, 0)->argument);
    CHECK_EQUAL(SD_RESPONSE_SHORT_NO_CRC, find_command(&card, 41, 0)->response);
    CHECK_EQUAL(SD_RESPONSE_LONG, find_command(&card, 2, 0)->response);
    CHECK_EQUAL(0x12340000, find_command(&card, 9, 0)->argument);
    CHECK_EQUAL(SD_RESPONSE_LONG, find_command(&card, 9, 0)->response);
    CHECK_EQUAL(0x12340000, find_command(&card, 7, 0)->argument);
    CHECK_EQUAL(SD_RESPONSE_SHORT_BUSY, find_command(&card, 7, 0)->response);
    CHECK_EQUAL(0, find_command(&card, 42, 0)->argument);
    CHECK_EQUAL(2, find_command(&card, 6, 0)->argument);
    CHECK(find_command(&card, 16, 0) == NULL);
}

static void the_card_is_asked_again_every_10_ms_while_busy(void)
{
    FakeCard card = sdhc_card(100);
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(sd_card_init(&sd, &host));
    CHECK(card.delays_ms >= BUSY_TRIES * 10);
}

static void a_v1_card_is_standard_capacity_with_a_block_length_set(void)
{
    FakeCard card = sdsc_v1_card();
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(sd_card_init(&sd, &host));
    CHECK(!sd.high_capacity);
    CHECK_EQUAL(0x00FF8000, find_command(&card, 41, 0)->argument);
    CHECK_EQUAL(512, find_command(&card, 16, 0)->argument);
    CHECK_EQUAL(4096u << 9, sd.sector_count);
}

static void standard_capacity_cards_take_byte_addresses(void)
{
    SdCard standard = {.high_capacity = false};
    SdCard high = {.high_capacity = true};
    CHECK_EQUAL(10 * 512, sd_card_address(&standard, 10));
    CHECK_EQUAL(10, sd_card_address(&high, 10));
}

static void a_zero_rca_is_asked_for_again(void)
{
    FakeCard card = sdhc_card(100);
    card.zero_rca_answers = 2;
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(sd_card_init(&sd, &host));
    CHECK_EQUAL(0x1234, sd.rca);
    CHECK(find_command(&card, 3, 2) != NULL);
}

static void a_card_that_always_gives_a_zero_rca_fails(void)
{
    FakeCard card = sdhc_card(100);
    card.zero_rca_answers = 10;
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(!sd_card_init(&sd, &host));
    CHECK(card.stopped);
}

static void a_card_without_a_wide_bus_stays_at_1_bit(void)
{
    FakeCard card = sdhc_card(100);
    card.wide_bus = false;
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(sd_card_init(&sd, &host));
    CHECK_EQUAL(1, sd.bus_width);
    CHECK_EQUAL(0, card.bus_width);
}

static void a_card_that_stays_busy_fails_after_100_tries(void)
{
    FakeCard card = sdhc_card(100);
    card.busy_tries = 1000;
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(!sd_card_init(&sd, &host));
    CHECK(find_command(&card, 41, 99) != NULL);
    CHECK(find_command(&card, 41, 100) == NULL);
    CHECK(card.stopped);
}

static void a_silent_card_gives_up_after_three_unanswered_tries(void)
{
    FakeCard card = sdhc_card(100);
    card.silent = true;
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(!sd_card_init(&sd, &host));
    CHECK(find_command(&card, 55, 2) != NULL);
    CHECK(find_command(&card, 55, 3) == NULL);
    CHECK(card.delays_ms < 50);
}

static void a_high_capacity_card_that_missed_cmd8_is_refused_not_byte_addressed(void)
{
    FakeCard card = sdhc_card(100);
    card.answers_cmd8 = false;
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(!sd_card_init(&sd, &host));
    CHECK(card.stopped);
}

static void no_card_fails_without_sending_commands(void)
{
    FakeCard card = sdhc_card(100);
    card.present = false;
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(!sd_card_init(&sd, &host));
    CHECK_EQUAL(0, card.log_count);
}

static void a_wrong_cmd8_echo_fails(void)
{
    FakeCard card = sdhc_card(100);
    card.cmd8_echo = 0x0AA;
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(!sd_card_init(&sd, &host));
    CHECK(find_command(&card, 41, 0) == NULL);
}

static void the_csd_erase_unit_is_available_when_sd_status_is_unknown(void)
{
    FakeCard card = sdsc_v1_card();
    set_bits(card.csd, 22, 4, 9);
    set_bits(card.csd, 39, 7, 31);
    SdHost host = host_for(&card);
    SdCard sd;
    CHECK(sd_card_init(&sd, &host));
    CHECK_EQUAL(32, sd.erase_sectors);
    set_bits(card.csd, 46, 1, 1);
    CHECK(sd_card_init(&sd, &host));
    CHECK_EQUAL(1, sd.erase_sectors);
}

static void presence_rejects_bounce_and_handles_clock_wrap(void)
{
    CardPresence state = {0};
    CHECK(!card_presence_update(&state, true, 100));
    CHECK(!card_presence_update(&state, false, 110));
    CHECK(!card_presence_update(&state, true, 120));
    CHECK(!card_presence_update(&state, true, 149));
    CHECK(card_presence_update(&state, true, 150));
    CHECK(card_presence_update(&state, false, UINT32_MAX - 10));
    CHECK(card_presence_update(&state, false, 18));
    CHECK(!card_presence_update(&state, false, 19));
}

static void eject_survives_bounce_until_a_debounced_removal(void)
{
    CardPresence state = {.present = true, .candidate = true, .ejected = true};
    CHECK(card_presence_update(&state, false, 100));
    CHECK(card_presence_update(&state, true, 110));
    CHECK(state.ejected);
    CHECK(card_presence_update(&state, false, 200));
    CHECK(!card_presence_update(&state, false, 230));
    CHECK(!state.ejected);
    CHECK(!card_presence_update(&state, true, 240));
    CHECK(card_presence_update(&state, true, 270));
}

static void a_failed_start_is_retried_each_second_up_to_ten_times(void)
{
    CardStart start = {0};
    CHECK(card_start_due(&start, UINT32_MAX - 100));
    CHECK(!card_start_due(&start, UINT32_MAX));
    CHECK(card_start_due(&start, CARD_START_RETRY_MS - 101));
    uint32_t now = CARD_START_RETRY_MS - 101;
    for (int retry = 2; retry < CARD_START_TRIES; retry++) {
        now += CARD_START_RETRY_MS;
        CHECK(!card_start_due(&start, now - 1));
        CHECK(card_start_due(&start, now));
    }
    CHECK_EQUAL(CARD_START_TRIES, start.tries);
    CHECK(!card_start_due(&start, now + 10 * CARD_START_RETRY_MS));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(eject_survives_bounce_until_a_debounced_removal),
        TEST_CASE(a_failed_start_is_retried_each_second_up_to_ten_times),
        TEST_CASE(presence_rejects_bounce_and_handles_clock_wrap),
        TEST_CASE(an_sdhc_card_initialises_with_its_size_and_a_4_bit_bus),
        TEST_CASE(high_speed_requires_both_a_supported_check_and_a_successful_switch),
        TEST_CASE(mmc_dividers_keep_each_speed_at_or_below_its_limits),
        TEST_CASE(emmc_requires_a_supported_speed_and_clean_ready_transfer_status),
        TEST_CASE(the_commands_follow_the_sd_init_order),
        TEST_CASE(command_arguments_and_responses_match_the_facts_sheet),
        TEST_CASE(the_card_is_asked_again_every_10_ms_while_busy),
        TEST_CASE(a_v1_card_is_standard_capacity_with_a_block_length_set),
        TEST_CASE(standard_capacity_cards_take_byte_addresses),
        TEST_CASE(a_zero_rca_is_asked_for_again),
        TEST_CASE(a_card_that_always_gives_a_zero_rca_fails),
        TEST_CASE(a_card_without_a_wide_bus_stays_at_1_bit),
        TEST_CASE(a_card_that_stays_busy_fails_after_100_tries),
        TEST_CASE(no_card_fails_without_sending_commands),
        TEST_CASE(a_silent_card_gives_up_after_three_unanswered_tries),
        TEST_CASE(a_high_capacity_card_that_missed_cmd8_is_refused_not_byte_addressed),
        TEST_CASE(a_wrong_cmd8_echo_fails),
        TEST_CASE(the_csd_erase_unit_is_available_when_sd_status_is_unknown),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
