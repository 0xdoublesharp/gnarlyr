#include <stddef.h>

#include "core/deadline.h"
#include "core/seam.h"
#include "device/usb_fifo.h"
#include "fifo_words.h"
#include "hal/clock.h"
#include "usb.h"
#include "usb_registers.h"

enum {
    INTERFACE_STATUS = 0x81,
    ENDPOINT_STATUS = 0x82,
    ENDPOINT_FEATURE = 0x02,
    GET_STATUS = 0,
    SET_FEATURE = 3,
    DAC_HID_IN = 0x82
};

static void arm_setup(void)
{
    write_register(out_control(0) + 0x10u, EP0_OUT_SETUP_SIZE);
    set_bits(out_control(0), EP_ENABLE | EP_CLEAR_NAK);
}

static uint32_t transfer_packets(const UsbPort *port)
{
    uint32_t packets = TRANSFER_BYTE_LIMIT / port->bulk_packet;
    return packets < TRANSFER_PACKET_LIMIT ? packets : TRANSFER_PACKET_LIMIT;
}

static void arm_bulk_out(const UsbPort *port)
{
    uint32_t packets = transfer_packets(port);
    write_register(out_control(BULK_ENDPOINT) + 0x10u,
                   packets << PACKET_COUNT_SHIFT | packets * port->bulk_packet);
    set_bits(out_control(BULK_ENDPOINT), EP_ENABLE | EP_CLEAR_NAK);
}

static uint32_t tx_space(const UsbPort *port, uint32_t endpoint)
{
    if (port->link.dedicated_fifos) {
        return read_register(in_control(endpoint) + 0x18u) & TX_SPACE_MASK;
    }
    uint32_t status = read_register(GNPTXSTS);
    bool queue_has_room = (status & TX_QUEUE_MASK) != 0;
    return queue_has_room ? status & TX_SPACE_MASK : 0;
}

static bool wait_for_space(const UsbPort *port, uint32_t endpoint, uint32_t words)
{
    uint32_t start = hal_clock_milliseconds();
    while (tx_space(port, endpoint) < words) {
        if (hal_clock_milliseconds() - start > SPACE_TIMEOUT_MS) {
            return false;
        }
    }
    return true;
}

static bool send_control_packet(const UsbPort *port, const uint8_t *bytes, uint32_t length)
{
    uint32_t words = (length + 3) / 4;
    if (!port->link.dedicated_fifos && !wait_for_space(port, 0, words)) {
        return false;
    }
    write_register(in_control(0) + 0x10u, ONE_PACKET | length);
    set_bits(in_control(0), EP_ENABLE | EP_CLEAR_NAK);
    for (uint32_t word = 0; word < words; word++) {
        uint32_t value = 0;
        for (uint32_t byte = 0; byte < 4 && word * 4 + byte < length; byte++) {
            value |= (uint32_t)bytes[word * 4 + byte] << (8 * byte);
        }
        write_register(fifo(0), value);
    }
    return true;
}

static bool send_control(const UsbPort *port, UsbReply reply, uint32_t requested)
{
    uint32_t left = reply.length;
    const uint8_t *bytes = reply.data;
    while (left > USB_PACKET_SIZE || (left == USB_PACKET_SIZE && reply.length < requested)) {
        if (!send_control_packet(port, bytes, USB_PACKET_SIZE) ||
            !wait_clear(in_control(0), EP_ENABLE, SPACE_TIMEOUT_MS)) {
            return false;
        }
        bytes += USB_PACKET_SIZE;
        left -= USB_PACKET_SIZE;
    }
    return send_control_packet(port, bytes, left);
}

static void handle_receive(UsbPort *port);
static bool receive_blocked(const UsbPort *port);

static uint32_t next_frame_parity(void)
{
    return (read_register(DSTS) & DSTS_ODD_FRAME) != 0 ? EP_SET_EVEN_FRAME : EP_SET_ODD_FRAME;
}

static __attribute__((noinline)) void arm_iso_out(const UsbPort *port)
{
    uint32_t packet = usb_audio_packet_size(port->audio);
    write_register(out_control(BULK_ENDPOINT) + 0x10u, ONE_PACKET | packet);
    write_register(out_control(BULK_ENDPOINT),
                   packet | EP_ISO_ACTIVE | EP_ENABLE | EP_CLEAR_NAK | next_frame_parity());
}

static bool streaming(const UsbPort *port)
{
    return port->audio != NULL && port->audio->alt != 0;
}

static void drain_receive_fifo(UsbPort *port)
{
    bool deferred = port->defer_control;
    port->defer_control = true;
    for (int step = 0; step < RX_DRAIN_LIMIT && (read_register(GINTSTS) & GINT_RX_LEVEL) != 0 &&
                       !receive_blocked(port);
         step++) {
        handle_receive(port);
    }
    port->defer_control = deferred;
}

static __attribute__((noinline)) void wait_register(UsbPort *port, uint32_t offset, uint32_t mask,
                                                    uint32_t target)
{
    uint32_t start = hal_clock_milliseconds();
    while ((read_register(offset) & mask) != target &&
           !deadline_elapsed(hal_clock_milliseconds(), start, ENDPOINT_DISABLE_TIMEOUT_MS)) {
        if (port != NULL) {
            drain_receive_fifo(port);
        }
    }
}

static __attribute__((noinline)) void disable_endpoint(UsbPort *port, uint32_t endpoint)
{
    bool is_in = port == NULL;
    uint32_t control = is_in ? in_control(endpoint) : out_control(endpoint);
    uint32_t value = read_register(control);
    bool enabled = (value & EP_ENABLE) != 0;
    if (enabled) {
        uint32_t mask = ~(EP_ENABLE | EP_DISABLE | EP_SET_EVEN_FRAME | EP_SET_ODD_FRAME);
        if (is_in) {
            write_register(control, (value & mask) | EP_SET_NAK);
            wait_register(NULL, control + 0x08u, EP_INT_IN_NAK_EFF, EP_INT_IN_NAK_EFF);
        } else {
            set_bits(DCTL, DCTL_SET_GLOBAL_OUT_NAK);
            wait_register(port, GINTSTS, GINT_GLOBAL_OUT_NAK, GINT_GLOBAL_OUT_NAK);
        }
        value = read_register(control);
        if ((value & EP_ENABLE) != 0) {
            write_register(control, (value & mask) | EP_DISABLE | EP_SET_NAK);
            wait_register(port, control, EP_ENABLE, 0);
        }
        write_register(control + 0x08u, EP_INT_DISABLED | (is_in ? EP_INT_IN_NAK_EFF : 0));
        if (!is_in) {
            set_bits(DCTL, DCTL_CLEAR_GLOBAL_OUT_NAK);
        }
    }
    if (is_in && (endpoint != 0 || enabled)) {
        write_register(GRSTCTL, GRSTCTL_TX_FLUSH | (endpoint << 6));
        wait_register(NULL, GRSTCTL, GRSTCTL_TX_FLUSH, 0);
    }
}

static void start_stream(UsbPort *port)
{
    if (!streaming(port)) {
        return;
    }
    uint32_t control = read_register(out_control(BULK_ENDPOINT));
    if ((control & EP_ENABLE) != 0) {
        if ((control & EP_NAK_STATUS) == 0) {
            return;
        }
        disable_endpoint(port, BULK_ENDPOINT);
    }
    arm_iso_out(port);
}

static void handle_incomplete_iso(UsbPort *port, bool rearmed)
{
    write_register(GINTSTS, GINT_INCOMPLETE_ISO_OUT);
    if (rearmed || !streaming(port)) {
        return;
    }
    if ((read_register(out_control(BULK_ENDPOINT)) & EP_ENABLE) != 0) {
        disable_endpoint(port, BULK_ENDPOINT);
        arm_iso_out(port);
    }
}

static void clear_endpoint_interrupts(uint32_t endpoint)
{
    write_register(in_control(endpoint) + 0x08u, EP_INT_ALL);
    write_register(out_control(endpoint) + 0x08u, EP_INT_ALL);
}

static void handle_reset(UsbPort *port)
{
    write_register(DCFG, read_register(DCFG) & ~DCFG_ADDRESS_MASK);
    for (uint32_t endpoint = 0; endpoint < 4; endpoint++) {
        clear_endpoint_interrupts(endpoint);
        if (endpoint > 0) {
            if ((read_register(in_control(endpoint)) & EP_ENABLE) != 0) {
                disable_endpoint(NULL, endpoint);
                write_register(in_control(endpoint), EP_IDLE_CONTROL);
            }
            if ((read_register(out_control(endpoint)) & EP_ENABLE) != 0) {
                disable_endpoint(port, endpoint);
                write_register(out_control(endpoint), EP_IDLE_CONTROL);
            }
        }
    }
    usb_flush_fifos();
    port->configured = false;
    port->device.configuration = 0;
    if (port->resets < UINT8_MAX) {
        port->resets++;
    }
    port->in_busy = false;
    port->in_left = 0;
    port->control_out_pending = false;
    port->control_data_pending = false;
    port->setup_pending = false;
    port->setup_ready = false;
    port->hid = (UsbAudioHid){0};
    if (port->msc != NULL) {
        msc_reset(port->msc);
    }
    if (port->audio != NULL) {
        usb_audio_reset(port->audio);
    }
    arm_setup();
    write_register(GINTSTS, GINT_USB_RESET);
}

static void activate_bulk_endpoints(const UsbPort *port)
{
    uint32_t tx_fifo = port->link.dedicated_fifos ? 1u << EP_TX_FIFO_SHIFT : 0;
    write_register(in_control(BULK_ENDPOINT), port->bulk_packet | tx_fifo | EP_BULK_ACTIVE);
    write_register(out_control(BULK_ENDPOINT), port->bulk_packet | EP_BULK_ACTIVE);
    set_bits(DAINTMSK, DAINT_EP1);
    set_bits(DCTL, DCTL_CLEAR_GLOBAL_IN_NAK);
    arm_bulk_out(port);
}

static UsbFifoLayout audio_fifos(void)
{
    return usb_fifo_audio(read_register(GHWCFG3) >> HWCFG3_DEPTH_SHIFT);
}

static void activate_audio_endpoints(const UsbPort *port)
{
    UsbFifoLayout layout = audio_fifos();
    write_register(GRXFSIZ, layout.rx);
    write_register(GNPTXFSIZ, layout.nptx);
    write_register(DIEPTXF1, layout.tx1);
    write_register(DIEPTXF2, layout.tx2);
    usb_flush_fifos();
    write_register(out_control(1), USB_AUDIO_MAX_PACKET | EP_ISO_ACTIVE);
    write_register(in_control(1), 4 | EP_ISO_ACTIVE | (1u << EP_TX_FIFO_SHIFT));
    write_register(in_control(2), 2 | EP_INTERRUPT_ACTIVE | (2u << EP_TX_FIFO_SHIFT));
    set_bits(DAINTMSK, DAINT_EP1 | (1u << 2));
    set_bits(GINTMSK, GINT_INCOMPLETE_ISO_OUT | GINT_INCOMPLETE_ISO_IN);
    set_bits(DCTL, DCTL_CLEAR_GLOBAL_IN_NAK);
    (void)port;
}

static void activate_endpoints(const UsbPort *port)
{
    if (port->audio == NULL) {
        activate_bulk_endpoints(port);
    } else {
        activate_audio_endpoints(port);
    }
}

static void change_stream(UsbPort *port)
{
    disable_endpoint(port, 1);
    disable_endpoint(NULL, 1);
    start_stream(port);
}

static uint32_t endpoint_control(uint16_t endpoint)
{
    uint32_t number = endpoint & 0x0Fu;
    return (endpoint & 0x80u) != 0 ? in_control(number) : out_control(number);
}

static void reset_storage(UsbPort *port)
{
    msc_reset(port->msc);
    port->in_busy = false;
    port->in_left = 0;
}

static void configure_endpoints(UsbPort *port, bool configured)
{
    port->configured = configured;
    if (configured) {
        activate_endpoints(port);
        return;
    }
    disable_endpoint(port, 1);
    disable_endpoint(NULL, 1);
    if (port->audio != NULL) {
        disable_endpoint(NULL, 2);
    } else {
        reset_storage(port);
    }
}

static void apply_action(UsbPort *port, UsbSetup setup, UsbReply reply)
{
    switch (reply.action) {
    case USB_ACTION_SET_ADDRESS:
        write_register(DCFG, (read_register(DCFG) & ~DCFG_ADDRESS_MASK) |
                                 ((uint32_t)reply.value << DCFG_ADDRESS_SHIFT));
        break;
    case USB_ACTION_CONFIGURE:
        configure_endpoints(port, reply.value == 1);
        break;
    case USB_ACTION_STREAM:
        change_stream(port);
        break;
    case USB_ACTION_CLEAR_HALT: {
        uint32_t control = endpoint_control(reply.value);
        write_register(control, (read_register(control) & ~EP_STALL) | EP_SET_DATA0);
        break;
    }
    case USB_ACTION_MSC_RESET:
        reset_storage(port);
        break;
    case USB_ACTION_NONE:
        if (setup.request_type == ENDPOINT_FEATURE && setup.request == SET_FEATURE) {
            set_bits(endpoint_control(reply.value), EP_STALL);
        }
        break;
    }
}

static bool answer_setup(UsbPort *port, UsbSetup setup, UsbReply reply)
{
    if (reply.kind == USB_REPLY_STALL) {
        return false;
    }
    apply_action(port, setup, reply);
    if (setup.request_type == ENDPOINT_STATUS && setup.request == GET_STATUS &&
        (setup.index & 0x0Fu) != 0) {
        port->device.scratch[0] = (read_register(endpoint_control(setup.index)) & EP_STALL) != 0;
    }
    return send_control(port, reply, setup.length);
}

static void finish_setup(UsbPort *port, UsbSetup setup, UsbReply reply)
{
    if (port->audio != NULL && reply.action == USB_ACTION_CONFIGURE && reply.value == 1 &&
        !audio_fifos().fits) {
        port->device.configuration = 0;
        reply = (UsbReply){.kind = USB_REPLY_STALL};
    }
    if (reply.kind != USB_REPLY_STALL && port->audio != NULL &&
        (port->audio->changes & USB_AUDIO_CHANGED_FORMAT) != 0 && port->format_changed != NULL) {
        port->format_changed();
    }
    if (!answer_setup(port, setup, reply)) {
        set_bits(in_control(0), EP_STALL | EP_CLEAR_NAK);
        set_bits(out_control(0), EP_STALL);
        arm_setup();
    }
}

static void trace_setup(UsbPort *port, UsbSetup setup, UsbReplyKind kind)
{
    HalUsbSetupOutcome outcome = kind == USB_REPLY_STALL     ? HAL_USB_SETUP_STALLED
                                 : kind == USB_REPLY_RECEIVE ? HAL_USB_SETUP_RECEIVING
                                                             : HAL_USB_SETUP_ANSWERED;
    port->setups.setups[port->setups.seen % HAL_USB_SETUP_TRACE] = (HalUsbSetup){
        setup.request_type, setup.request, (uint8_t)outcome, setup.value, setup.length};
    port->setups.seen++;
}

static bool valid_control_target(const UsbPort *port, UsbSetup setup)
{
    if (setup.request_type == ENDPOINT_FEATURE) {
        return port->configured && (setup.index == DAC_HID_IN) == (port->audio != NULL);
    }
    if (setup.request == GET_STATUS && setup.request_type == ENDPOINT_STATUS) {
        return (setup.index & 0x0Fu) == 0 ||
               (port->configured && (setup.index != DAC_HID_IN || port->audio != NULL));
    }
    return setup.request_type != INTERFACE_STATUS || setup.request != GET_STATUS ||
           (port->configured && (setup.index == 0 || port->audio != NULL));
}

static void process_setup(UsbPort *port)
{
    disable_endpoint(NULL, 0);
    write_register(in_control(0), read_register(in_control(0)) & ~EP_STALL);
    write_register(out_control(0), read_register(out_control(0)) & ~EP_STALL);
    UsbSetup setup = usb_setup_parse((const uint8_t *)port->setup);
    UsbReply reply = {.kind = USB_REPLY_STALL};
    if (valid_control_target(port, setup)) {
        reply = port->audio != NULL ? usb_audio_setup(port->audio, setup)
                                    : usb_device_setup(&port->device, setup);
    }
    trace_setup(port, setup, reply.kind);
    port->setup_pending = false;
    port->setup_ready = false;
    if (reply.kind == USB_REPLY_RECEIVE) {
        port->control_out = setup;
        port->control_out_pending = true;
        return;
    }
    finish_setup(port, setup, reply);
}

static void finish_control_out(UsbPort *port, uint32_t length, const uint8_t *bytes)
{
    port->control_out_pending = false;
    UsbReply reply = usb_audio_receive(port->audio, port->control_out, bytes, (uint16_t)length);
    finish_setup(port, port->control_out, reply);
}

static void deliver(UsbPort *port, uint32_t endpoint, const uint8_t *bytes, uint32_t length)
{
    if (endpoint == 0 &&
        (port->setup_pending || (port->defer_control && port->control_out_pending))) {
        for (uint32_t index = 0; index < length && index < sizeof port->control_data; index++) {
            port->control_data[index] = bytes[index];
        }
        port->control_data_pending = true;
        port->control_data_length = (uint16_t)length;
    } else if (endpoint == 0 && port->control_out_pending) {
        finish_control_out(port, length, bytes);
    } else if (endpoint == BULK_ENDPOINT && port->audio == NULL) {
        msc_receive(port->msc, bytes, length);
    } else if (endpoint == BULK_ENDPOINT && port->receive_stream != NULL) {
        port->receive_stream(bytes, length);
    }
}

static void read_fifo(uint32_t *words, uint32_t capacity_words, uint32_t bytes)
{
    uint32_t count = (bytes + 3) / 4;
    if (count <= capacity_words) {
        fifo_words_read(words, USB_BASE + fifo(0), count);
        return;
    }
    for (uint32_t index = 0; index < count; index++) {
        uint32_t value = read_register(fifo(0));
        if (index < capacity_words) {
            words[index] = value;
        }
    }
}

static bool storage_packet(const UsbPort *port, uint32_t status)
{
    uint32_t type = (status >> RX_STATUS_TYPE_SHIFT) & RX_STATUS_TYPE_MASK;
    return type == PACKET_OUT_DATA && (status & RX_STATUS_ENDPOINT_MASK) == BULK_ENDPOINT &&
           port->audio == NULL && port->msc != NULL;
}

static bool receive_blocked(const UsbPort *port)
{
    bool storage = port->msc != NULL && port->audio == NULL;
    if (!storage || !msc_out_blocked(port->msc, port->bulk_packet)) {
        return false;
    }
    uint32_t status = read_register(GRXSTSR);
    uint32_t count = (status >> RX_STATUS_COUNT_SHIFT) & RX_STATUS_COUNT_MASK;
    return storage_packet(port, status) && msc_out_blocked(port->msc, count);
}

static void receive_storage(UsbPort *port, uint32_t count)
{
    uint8_t *slot = msc_out_slot(port->msc, count);
    uint32_t capacity = slot != NULL ? count : sizeof port->out_packet;
    uint8_t *bytes = slot != NULL ? slot : (uint8_t *)port->out_packet;
    if (slot != NULL) {
        fifo_words_read((uint32_t *)(void *)slot, USB_BASE + fifo(0), count / 4);
    } else {
        read_fifo((uint32_t *)(void *)bytes, capacity / 4, count);
    }
    msc_receive(port->msc, bytes, count < capacity ? count : capacity);
}

static void receive_setup(UsbPort *port, uint32_t count)
{
    port->control_data_pending = false;
    port->control_out_pending = false;
    port->setup_ready = false;
    port->setup_pending = count == sizeof port->setup;
    if (!port->setup_pending) {
        read_fifo(NULL, 0, count);
        return;
    }
    port->setup[0] = read_register(fifo(0));
    port->setup[1] = read_register(fifo(0));
}

static void handle_receive(UsbPort *port)
{
    if (receive_blocked(port)) {
        return;
    }
    uint32_t status = read_register(GRXSTSP);
    uint32_t endpoint = status & RX_STATUS_ENDPOINT_MASK;
    uint32_t count = (status >> RX_STATUS_COUNT_SHIFT) & RX_STATUS_COUNT_MASK;
    uint32_t type = (status >> RX_STATUS_TYPE_SHIFT) & RX_STATUS_TYPE_MASK;
    if (storage_packet(port, status)) {
        receive_storage(port, count);
    } else if (type == PACKET_SETUP_DATA) {
        receive_setup(port, count);
    } else if (type == PACKET_OUT_DATA) {
        read_fifo(port->out_packet, sizeof port->out_packet / 4, count);
        uint32_t capacity = sizeof port->out_packet;
        deliver(port, endpoint, (const uint8_t *)port->out_packet,
                count < capacity ? count : capacity);
    }
}

static bool handle_out_endpoints(UsbPort *port)
{
    bool iso_rearmed = false;
    uint32_t pending = read_register(DAINT);
    if ((pending & (1u << 16)) != 0) {
        uint32_t events = read_register(out_control(0) + 0x08u);
        write_register(out_control(0) + 0x08u, events);
        port->setup_ready |= (events & EP_INT_SETUP) != 0 && port->setup_pending;
        if (port->setup_ready && !port->defer_control) {
            process_setup(port);
        }
        arm_setup();
    }
    if ((pending & (1u << (16 + BULK_ENDPOINT))) != 0) {
        uint32_t events = read_register(out_control(BULK_ENDPOINT) + 0x08u);
        write_register(out_control(BULK_ENDPOINT) + 0x08u, events);
        if ((events & EP_INT_TRANSFER_DONE) != 0 && port->audio == NULL) {
            arm_bulk_out(port);
        } else if ((events & EP_INT_TRANSFER_DONE) != 0 && streaming(port)) {
            arm_iso_out(port);
            iso_rearmed = true;
        }
    }
    return iso_rearmed;
}

static void handle_in_endpoints(UsbPort *port)
{
    uint32_t pending = read_register(DAINT);
    if ((pending & 1u) != 0) {
        write_register(in_control(0) + 0x08u, read_register(in_control(0) + 0x08u));
    }
    if (port->audio != NULL && (pending & (1u << 2)) != 0) {
        write_register(in_control(2) + 0x08u, read_register(in_control(2) + 0x08u));
    }
    if ((pending & (1u << BULK_ENDPOINT)) != 0) {
        uint32_t events = read_register(in_control(BULK_ENDPOINT) + 0x08u);
        write_register(in_control(BULK_ENDPOINT) + 0x08u, events);
        if ((events & EP_INT_TRANSFER_DONE) != 0) {
            port->in_busy = false;
        }
    }
}

static bool send_bulk(const UsbPort *port, const uint32_t *words, uint32_t length)
{
    uint32_t packet = port->bulk_packet;
    uint32_t packets = length == 0 ? 1 : (length + packet - 1) / packet;
    uint32_t total_words = (length + 3) / 4;
    write_register(in_control(BULK_ENDPOINT) + 0x10u, packets << PACKET_COUNT_SHIFT | length);
    set_bits(in_control(BULK_ENDPOINT), EP_ENABLE | EP_CLEAR_NAK);
    for (uint32_t sent = 0; sent < total_words;) {
        uint32_t chunk = total_words - sent < PACKET_WORDS ? total_words - sent : PACKET_WORDS;
        if (!wait_for_space(port, BULK_ENDPOINT, chunk)) {
            return false;
        }
        for (uint32_t index = 0; index < chunk; index++) {
            write_register(fifo(BULK_ENDPOINT), words[sent + index]);
        }
        sent += chunk;
    }
    return true;
}

static void arm_stream_in(UsbPort *port)
{
    uint32_t packet = port->bulk_packet;
    uint32_t length = msc_in_ready(port->msc, packet, transfer_packets(port));
    if (length == 0) {
        return;
    }
    uint32_t packets = (length + packet - 1) / packet;
    write_register(in_control(BULK_ENDPOINT) + 0x10u, packets << PACKET_COUNT_SHIFT | length);
    set_bits(in_control(BULK_ENDPOINT), EP_ENABLE | EP_CLEAR_NAK);
    port->in_busy = true;
    port->in_left = (uint16_t)length;
}

static void stream_in(UsbPort *port)
{
    if (port->in_left == 0 && !port->in_busy) {
        arm_stream_in(port);
    }
    uint32_t length = port->in_left < port->bulk_packet ? port->in_left : port->bulk_packet;
    uint32_t words = (length + 3) / 4;
    if (length == 0 || tx_space(port, BULK_ENDPOINT) < words) {
        return;
    }
    fifo_words_write(USB_BASE + fifo(BULK_ENDPOINT), msc_in_data(port->msc), words);
    msc_in_sent(port->msc, length);
    port->in_left = (uint16_t)(port->in_left - length);
}

static void pump_bulk_in(UsbPort *port)
{
    uint32_t *chunk = port->out_packet;
    uint32_t length = 0;
    if (!port->configured || port->msc == NULL) {
        return;
    }
    if (msc_in_streaming(port->msc) || port->in_left != 0) {
        stream_in(port);
        return;
    }
    if (port->in_busy) {
        msc_prefetch(port->msc);
        return;
    }
    if (msc_next_in(port->msc, (uint8_t *)chunk, BULK_CHUNK_BYTES, &length)) {
        port->in_busy = send_bulk(port, chunk, length);
    }
}

static bool send_periodic(const UsbPort *port, uint32_t endpoint, uint32_t word, uint32_t length,
                          bool iso)
{
    uint32_t control = read_register(in_control(endpoint));
    if ((control & EP_ENABLE) != 0 || tx_space(port, endpoint) < 1) {
        return false;
    }
    uint32_t parity = iso ? next_frame_parity() : 0;
    uint32_t mask = ~(EP_DISABLE | EP_SET_NAK | EP_SET_EVEN_FRAME | EP_SET_ODD_FRAME);
    write_register(in_control(endpoint) + 0x10u, ONE_PACKET | length | (iso ? 1u << 29 : 0));
    write_register(in_control(endpoint), (control & mask) | EP_ENABLE | EP_CLEAR_NAK | parity);
    write_register(fifo(endpoint), word);
    return true;
}

static void pump_audio_in(UsbPort *port)
{
    if (!port->configured || port->audio == NULL) {
        return;
    }
    uint16_t tick = (uint16_t)((read_register(DSTS) >> 8) & 0x3FFF);
    if (streaming(port) && usb_audio_asynchronous(port->audio) && port->feedback != NULL &&
        (read_register(in_control(1)) & EP_ENABLE) == 0) {
        send_periodic(port, 1, port->feedback(tick), 4, true);
    }
    uint16_t usage = 0;
    uint32_t now = hal_clock_milliseconds();
    bool pending = usb_audio_hid_report(&port->hid, &usage);
    bool idle = port->audio->hid_idle != 0 &&
                deadline_elapsed(now, port->hid_sent_ms, 4u * port->audio->hid_idle);
    if ((pending || idle) && send_periodic(port, 2, usage, 2, false)) {
        port->audio->hid_usage = usage;
        port->hid_sent_ms = now;
        if (pending) {
            usb_audio_hid_sent(&port->hid);
        }
    }
}

static UsbWriteTiming write_timing;

UsbWriteTiming *usb_port_write_timing(void)
{
    return &write_timing;
}

void usb_port_settle_timing(void)
{
    uint32_t now = hal_clock_microseconds();
    write_timing.spent_us[write_timing.wait] += now - write_timing.at_us;
    write_timing.at_us = now;
}

static void follow_timing(const UsbPort *port)
{
    uint8_t wait = (uint8_t)msc_write_wait(port->msc, port->bulk_packet);
    if (wait != write_timing.wait) {
        usb_port_settle_timing();
        write_timing.wait = wait;
    }
}

static bool storage_active(const UsbPort *port)
{
    const Msc *msc = port->msc;
    return msc->streaming || msc->phase == MSC_PHASE_STATUS || port->in_left != 0;
}

static bool burst_over(const UsbPort *port, UsbBurst *burst, uint32_t turn)
{
    bool timed = (turn & STREAM_CLOCK_TURNS) == 0;
    if (storage_active(port)) {
        burst->served = true;
        burst->listening = false;
        return timed &&
               deadline_elapsed(hal_clock_milliseconds(), burst->start_ms, STREAM_BURST_MS);
    }
    if (!burst->served || port->yield_when_idle) {
        return true;
    }
    uint32_t now = hal_clock_milliseconds();
    if (!burst->listening) {
        burst->listening = true;
        burst->idle_ms = now;
    }
    return deadline_elapsed(now, burst->idle_ms, STREAM_LISTEN_MS);
}

static void stream_burst(UsbPort *port)
{
    UsbBurst burst = {.start_ms = hal_clock_milliseconds()};
    uint32_t others = GINT_USB_RESET | GINT_ENUMERATION_DONE;
    for (uint32_t turn = 1; !port->setup_pending && !burst_over(port, &burst, turn); turn++) {
        uint32_t events = read_register(GINTSTS);
        if ((events & others) != 0) {
            return;
        }
        if ((events & GINT_RX_LEVEL) != 0) {
            handle_receive(port);
        }
        if ((events & GINT_OUT_ENDPOINT) != 0) {
            handle_out_endpoints(port);
        }
        if ((events & GINT_IN_ENDPOINT) != 0) {
            handle_in_endpoints(port);
        }
        msc_pump(port->msc);
        if (port->msc->phase != MSC_PHASE_DATA_OUT) {
            pump_bulk_in(port);
        }
        follow_timing(port);
    }
}

CORE_SEAM bool usb_wait_for_host(UsbPort *port, uint32_t timeout_ms)
{
    uint32_t start = hal_clock_milliseconds();
    while (!deadline_elapsed(hal_clock_milliseconds(), start, timeout_ms)) {
        if ((read_register(GINTSTS) & GINT_USB_RESET) != 0) {
            handle_reset(port);
            return true;
        }
    }
    return false;
}

static void take_speed(UsbPort *port)
{
    uint32_t speed = (read_register(DSTS) >> DSTS_SPEED_SHIFT) & DSTS_SPEED_MASK;
    bool high = port->link.high_speed_allowed && speed == DSTS_HIGH_SPEED;
    usb_device_set_speed(&port->device, high ? USB_SPEED_HIGH : USB_SPEED_FULL);
    port->bulk_packet = usb_device_bulk_packet_size(&port->device);
    port->enumerated = true;
}

CORE_SEAM void usb_poll(UsbPort *port)
{
    uint32_t events = read_register(GINTSTS);
    if ((events & GINT_USB_RESET) != 0) {
        handle_reset(port);
    }
    if ((events & GINT_ENUMERATION_DONE) != 0) {
        take_speed(port);
        arm_setup();
        write_register(GINTSTS, GINT_ENUMERATION_DONE);
    }
    if (!port->defer_control && port->setup_pending && port->setup_ready) {
        process_setup(port);
    }
    if (!port->defer_control && !port->setup_pending && port->control_data_pending) {
        port->control_data_pending = false;
        if (port->control_out_pending) {
            finish_control_out(port, port->control_data_length, port->control_data);
        }
    }
    events = read_register(GINTSTS);
    if ((events & GINT_RX_LEVEL) != 0) {
        handle_receive(port);
    }
    bool iso_rearmed = false;
    if ((events & GINT_OUT_ENDPOINT) != 0) {
        iso_rearmed = handle_out_endpoints(port);
    }
    if ((events & GINT_IN_ENDPOINT) != 0) {
        handle_in_endpoints(port);
    }
    if (port->audio != NULL) {
        if ((events & GINT_INCOMPLETE_ISO_OUT) != 0) {
            handle_incomplete_iso(port, iso_rearmed);
        }
        if ((events & GINT_INCOMPLETE_ISO_IN) != 0) {
            disable_endpoint(NULL, 1);
            write_register(GINTSTS, GINT_INCOMPLETE_ISO_IN);
        }
        if (port->configured && streaming(port) &&
            (events & (GINT_RX_LEVEL | GINT_OUT_ENDPOINT)) == 0) {
            start_stream(port);
        }
    }
    pump_bulk_in(port);
    pump_audio_in(port);
    if (port->msc != NULL && port->audio == NULL) {
        follow_timing(port);
        stream_burst(port);
    }
}

void usb_poll_audio(UsbPort *port)
{
    bool deferred = port->defer_control;
    port->defer_control = true;
    usb_poll(port);
    port->defer_control = deferred;
}

bool usb_reply_pending(const UsbPort *port)
{
    return port->in_busy || port->msc->phase == MSC_PHASE_STATUS ||
           port->msc->phase == MSC_PHASE_FINISH;
}

bool usb_port_connect(UsbPort *port)
{
    port->bulk_packet = USB_PACKET_SIZE;
    usb_device_set_speed(&port->device, USB_SPEED_FULL);
    return usb_connect(&port->link);
}
