#include "check.h"
#include "device/usb_fifo.h"

enum {
    PLAYER_DEPTH = 0x134,
    LARGE_DEPTH = 0x400
};

static uint32_t end_of(uint32_t size)
{
    return (size & 0xFFFFu) + (size >> 16);
}

static uint32_t layout_end(UsbFifoLayout layout)
{
    uint32_t end = layout.rx;
    uint32_t sizes[] = {layout.nptx, layout.tx1, layout.tx2};
    for (uint32_t index = 0; index < 3; index++) {
        if (sizes[index] != 0 && end_of(sizes[index]) > end) {
            end = end_of(sizes[index]);
        }
    }
    return end;
}

static void the_players_core_keeps_one_bulk_packet_and_gives_the_rest_to_receiving(void)
{
    UsbFifoLayout layout = usb_fifo_storage(PLAYER_DEPTH, false);
    CHECK(layout.fits);
    CHECK_EQUAL(0xB4u, layout.rx);
    CHECK_EQUAL(0x008000B4u, layout.nptx);
    CHECK_EQUAL(0u, layout.tx1);
    CHECK_EQUAL((uint32_t)PLAYER_DEPTH, layout_end(layout));
}

static void the_players_core_fits_the_usb_dac(void)
{
    UsbFifoLayout layout = usb_fifo_audio(PLAYER_DEPTH);
    CHECK(layout.fits);
    CHECK_EQUAL(0x104u, layout.rx);
    CHECK_EQUAL(0x00100104u, layout.nptx);
    CHECK_EQUAL(0x00100114u, layout.tx1);
    CHECK_EQUAL(0x00100124u, layout.tx2);
    CHECK_EQUAL((uint32_t)PLAYER_DEPTH, layout_end(layout));
    CHECK(layout.rx >= USB_FIFO_AUDIO_RX_MINIMUM);
}

static void large_cores_keep_the_storage_and_audio_layouts_they_had(void)
{
    UsbFifoLayout shared = usb_fifo_storage(LARGE_DEPTH, false);
    CHECK_EQUAL(0x120u, shared.rx);
    CHECK_EQUAL(0x01000120u, shared.nptx);
    UsbFifoLayout dedicated = usb_fifo_storage(LARGE_DEPTH, true);
    CHECK_EQUAL(0x120u, dedicated.rx);
    CHECK_EQUAL(0x00200120u, dedicated.nptx);
    CHECK_EQUAL(0x01000140u, dedicated.tx1);
    UsbFifoLayout audio = usb_fifo_audio(LARGE_DEPTH);
    CHECK_EQUAL(0x120u, audio.rx);
    CHECK_EQUAL(0x00200120u, audio.nptx);
    CHECK_EQUAL(0x00100140u, audio.tx1);
    CHECK_EQUAL(0x00100150u, audio.tx2);
}

static void a_core_too_small_for_a_layout_says_so(void)
{
    CHECK(!usb_fifo_storage(0x100, false).fits);
    CHECK(!usb_fifo_audio(0xC0).fits);
}

static void every_layout_that_fits_ends_inside_the_fifo_ram(void)
{
    for (uint32_t depth = 0x40; depth <= LARGE_DEPTH; depth++) {
        UsbFifoLayout layouts[] = {usb_fifo_storage(depth, false), usb_fifo_storage(depth, true),
                                   usb_fifo_audio(depth)};
        for (uint32_t index = 0; index < 3; index++) {
            if (layouts[index].fits) {
                CHECK(layout_end(layouts[index]) <= depth);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_players_core_keeps_one_bulk_packet_and_gives_the_rest_to_receiving),
        TEST_CASE(the_players_core_fits_the_usb_dac),
        TEST_CASE(large_cores_keep_the_storage_and_audio_layouts_they_had),
        TEST_CASE(a_core_too_small_for_a_layout_says_so),
        TEST_CASE(every_layout_that_fits_ends_inside_the_fifo_ram),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
