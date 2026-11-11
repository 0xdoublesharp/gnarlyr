#include <string.h>

#include "check.h"
#include "core/debug_script.h"
#include "core/key.h"
#include "core/key_script.h"

static DebugCommand parse(const char *line)
{
    size_t offset = 0;
    return debug_script_parse(debug_script_next_line(line, strlen(line), &offset));
}

static bool span_is(DebugSpan span, const char *text)
{
    return span.length == strlen(text) && memcmp(span.text, text, span.length) == 0;
}

static DebugScriptRun run;

static const DebugScriptRun *load_for(const char *text, const char *version, bool restart_pending)
{
    size_t length = strlen(text);
    memset(&run, 0xA5, sizeof run);
    memcpy(run.text, text, length < DEBUG_SCRIPT_BYTES ? length : DEBUG_SCRIPT_BYTES);
    debug_script_load(&run, (uint32_t)length, (DebugScriptHost){version, restart_pending});
    return &run;
}

static const DebugScriptRun *load(const char *text)
{
    return load_for(text, "1.0.0-abc1234", false);
}

static void lines_are_split_at_line_feeds_and_trimmed(void)
{
    const char text[] = "  play \r\n\nwait 5\n";
    size_t offset = 0;
    CHECK(span_is(debug_script_next_line(text, strlen(text), &offset), "play"));
    CHECK_EQUAL(0, debug_script_next_line(text, strlen(text), &offset).length);
    CHECK(span_is(debug_script_next_line(text, strlen(text), &offset), "wait 5"));
    CHECK_EQUAL(strlen(text), offset);
}

static void a_last_line_without_a_line_feed_counts(void)
{
    CHECK_EQUAL(3, debug_script_line_count("a\nb\nc", 5));
    CHECK_EQUAL(3, debug_script_line_count("a\nb\nc\n", 6));
    CHECK_EQUAL(0, debug_script_line_count("", 0));
}

static void blank_lines_and_comments_are_not_commands(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_BLANK, parse("").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BLANK, parse("   \t").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BLANK, parse("# open the music list").kind);
}

static void a_key_list_is_the_simulators_grammar(void)
{
    DebugCommand command = parse("play,down,play:long");
    CHECK_EQUAL(DEBUG_COMMAND_KEYS, command.kind);
    CHECK(span_is(command.argument, "play,down,play:long"));
    CHECK_EQUAL(DEBUG_COMMAND_KEYS, parse("back").kind);
    CHECK_EQUAL(DEBUG_COMMAND_KEYS, parse("power:held").kind);
}

static void anything_else_is_unknown_and_never_guessed(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_UNKNOWN, parse("jump").kind);
    CHECK_EQUAL(DEBUG_COMMAND_UNKNOWN, parse("play, down").kind);
    CHECK_EQUAL(DEBUG_COMMAND_UNKNOWN, parse("play down").kind);
    CHECK_EQUAL(DEBUG_COMMAND_UNKNOWN, parse("write 0x03004000 1").kind);
    CHECK_EQUAL(DEBUG_COMMAND_UNKNOWN, parse("waits 5").kind);
    CHECK(span_is(parse("jump high").argument, "jump high"));
}

static void wait_and_measure_take_milliseconds_up_to_a_minute(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_WAIT, parse("wait 500").kind);
    CHECK_EQUAL(500, parse("wait 500").number);
    CHECK_EQUAL(DEBUG_COMMAND_MEASURE, parse("measure 60000").kind);
    CHECK_EQUAL(60000, parse("measure 60000").number);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("wait").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("wait 0").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("wait 60001").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("wait 99999999999999").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("measure 5s").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("wait -5").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("wait 5 6").kind);
}

static void profile_takes_on_off_or_reset(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_PROFILE, parse("profile on").kind);
    CHECK_EQUAL(DEBUG_PROFILE_ON, parse("profile on").number);
    CHECK_EQUAL(DEBUG_PROFILE_OFF, parse("profile off").number);
    CHECK_EQUAL(DEBUG_PROFILE_RESET, parse("profile reset").number);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("profile").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("profile maybe").kind);
}

static void usb_sense_takes_normal_or_kept_on(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_USB_SENSE, parse("usb_sense kept_on").kind);
    CHECK_EQUAL(1, parse("usb_sense kept_on").number);
    CHECK_EQUAL(0, parse("usb_sense normal").number);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("usb_sense").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("usb_sense on").kind);
    const DebugScriptRun *loaded = load("build any\nusb_sense kept_on\nusb_sense normal\n");
    CHECK_EQUAL(2, loaded->op_count);
    CHECK_EQUAL(DEBUG_OP_USB_SENSE, loaded->ops[0].kind);
    CHECK_EQUAL(1, loaded->ops[0].first);
    CHECK_EQUAL(0, loaded->ops[1].first);
}

static void set_splits_the_setting_from_its_option_at_the_equals_sign(void)
{
    DebugCommand command = parse("set Screen timeout = 1 min");
    CHECK_EQUAL(DEBUG_COMMAND_SET, command.kind);
    CHECK(span_is(command.argument, "Screen timeout"));
    CHECK(span_is(command.value, "1 min"));
    CHECK(span_is(parse("set Orientation=Portrait, flipped").value, "Portrait, flipped"));
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("set Brightness 5").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("set =5").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("set Brightness=").kind);
}

static void a_screenshot_name_is_short_lower_case_and_plain(void)
{
    DebugCommand command = parse("screenshot now-playing");
    CHECK_EQUAL(DEBUG_COMMAND_SCREENSHOT, command.kind);
    CHECK(span_is(command.argument, "now-playing"));
    CHECK_EQUAL(DEBUG_COMMAND_SCREENSHOT, parse("screenshot a_1").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("screenshot").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("screenshot ../../boot").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("screenshot Home").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("screenshot two words").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("screenshot thirteen-char").kind);
}

static void peek_takes_one_word_in_hex_or_decimal(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_PEEK, parse("peek 0x500100E8").kind);
    CHECK_EQUAL(0x500100E8u, parse("peek 0x500100E8").number);
    CHECK_EQUAL(0x500100e8u, parse("PEEK 0X500100e8").number);
    CHECK_EQUAL(1342242024u, parse("peek 1342242024").number);
    CHECK_EQUAL(0xFFFFFFFFu, parse("peek 4294967295").number);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("peek").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("peek 4294967296").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("peek 0x").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("peek 0x1234567890").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("peek 0x5001zz00").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("peek 0x500100E8 1").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("peek grf").kind);
}

static void poke_takes_an_address_and_a_value(void)
{
    DebugCommand command = parse("poke 0x500100B4 0x000E0002");
    CHECK_EQUAL(DEBUG_COMMAND_POKE, command.kind);
    CHECK_EQUAL(0x500100B4u, command.number);
    CHECK_EQUAL(0x000E0002u, command.second);
    CHECK_EQUAL(7u, parse("poke 0x40180080   7").second);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("poke 0x500100B4").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("poke 0x500100B4 high").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("poke 0x500100B4 1 2").kind);
}

static void gpio_names_a_pin_and_what_to_do_with_it(void)
{
    DebugCommand command = parse("gpio 2.0 low");
    CHECK_EQUAL(DEBUG_COMMAND_GPIO, command.kind);
    CHECK_EQUAL(2u << DEBUG_PIN_PORT_SHIFT, command.number);
    CHECK_EQUAL(DEBUG_PIN_LOW, command.second);
    CHECK_EQUAL(14u, parse("gpio 0.14 high").number);
    CHECK_EQUAL(DEBUG_PIN_HIGH, parse("gpio 0.14 high").second);
    CHECK_EQUAL(DEBUG_PIN_INPUT, parse("gpio 0.14 input").second);
    CHECK_EQUAL(DEBUG_PIN_READ, parse("gpio 2.3 read").second);
    CHECK_EQUAL(DEBUG_PIN_FUNCTION | 1u << DEBUG_PIN_FUNCTION_SHIFT,
                parse("gpio 0.14 function 1").second);
    CHECK_EQUAL(DEBUG_PIN_FUNCTION, parse("gpio 0.14 function 0").second);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("gpio 0.14").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("gpio 0.32 low").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("gpio 14 low").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("gpio a.1 low").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("gpio 0.14 toggle").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("gpio 0.14 function").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("gpio 0.14 function 4").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("gpio 0.14 low 1").kind);
}

static void supply_and_screen_take_off_or_on(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_SUPPLY, parse("supply off").kind);
    CHECK_EQUAL(0, parse("supply off").number);
    CHECK_EQUAL(1, parse("supply on").number);
    CHECK_EQUAL(DEBUG_COMMAND_SCREEN, parse("screen off").kind);
    CHECK_EQUAL(0, parse("screen off").number);
    CHECK_EQUAL(1, parse("Screen ON").number);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("supply").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("supply low").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("screen dim").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("screen off now").kind);
    const DebugScriptRun *loaded =
        load("build any\nsupply off\nscreen off\nscreen on\nsupply on\n");
    CHECK_EQUAL(4, loaded->op_count);
    CHECK_EQUAL(DEBUG_OP_SUPPLY, loaded->ops[0].kind);
    CHECK_EQUAL(0, loaded->ops[0].first);
    CHECK_EQUAL(DEBUG_OP_SCREEN, loaded->ops[1].kind);
    CHECK_EQUAL(0, loaded->ops[1].first);
    CHECK_EQUAL(1, loaded->ops[2].first);
    CHECK_EQUAL(1, loaded->ops[3].first);
}

static void standby_takes_off_or_on(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_STANDBY, parse("standby off").kind);
    CHECK_EQUAL(0, parse("standby off").number);
    CHECK_EQUAL(1, parse("Standby ON").number);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("standby").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("standby later").kind);
    const DebugScriptRun *loaded = load("build any\nstandby off\nstandby on\n");
    CHECK_EQUAL(2, loaded->op_count);
    CHECK_EQUAL(DEBUG_OP_STANDBY, loaded->ops[0].kind);
    CHECK_EQUAL(0, loaded->ops[0].first);
    CHECK_EQUAL(1, loaded->ops[1].first);
}

static void soak_takes_one_to_180_minutes(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_SOAK, parse("soak 30").kind);
    CHECK_EQUAL(30, parse("soak 30").number);
    CHECK_EQUAL(1, parse("soak 1").number);
    CHECK_EQUAL(180, parse("soak 180").number);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("soak").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("soak 0").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("soak 181").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("soak 30 minutes").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("soak long").kind);
}

static void a_scripts_soaks_add_their_minutes_to_its_time_limit(void)
{
    CHECK_EQUAL(0, load("build any\nwait 60000\n")->soak_ms);
    const DebugScriptRun *loaded = load("build any\nsoak 30\nplay\nsoak 180\n");
    CHECK_EQUAL(3, loaded->op_count);
    CHECK_EQUAL(DEBUG_OP_SOAK, loaded->ops[0].kind);
    CHECK_EQUAL(30, loaded->ops[0].first);
    CHECK_EQUAL(180, loaded->ops[2].first);
    CHECK_EQUAL(210u * 60000u, loaded->soak_ms);
    CHECK_EQUAL(0, loaded->soak_count);
    CHECK_EQUAL(0, load("build 7777777\nsoak 30\n")->soak_ms);
}

static void rtc_read_lines_become_steps_and_other_actions_are_refused(void)
{
    const DebugScriptRun *loaded = load("build any\nrtc read\nrtc write\nrtc\nrtc set\n");
    CHECK_EQUAL(DEBUG_SCRIPT_PENDING, loaded->state);
    CHECK_EQUAL(2, loaded->op_count);
    CHECK_EQUAL(DEBUG_OP_RTC, loaded->ops[0].kind);
    CHECK_EQUAL(DEBUG_RTC_READ, loaded->ops[0].first);
    CHECK_EQUAL(2, loaded->ops[0].line);
    CHECK_EQUAL(DEBUG_OP_RTC, loaded->ops[1].kind);
    CHECK_EQUAL(DEBUG_RTC_SET, loaded->ops[1].first);
    CHECK_EQUAL(5, loaded->ops[1].line);
    CHECK_EQUAL(0, loaded->rtc_count);
    CHECK_EQUAL(DEBUG_LINE_BAD_ARGUMENT, loaded->status[2]);
    CHECK_EQUAL(DEBUG_LINE_BAD_ARGUMENT, loaded->status[3]);
}

static void probe_lines_keep_their_words_beside_the_steps(void)
{
    const DebugScriptRun *loaded =
        load("build any\npeek 0x500100E8\npoke 0x500100B4 0x000E0002\ngpio 0.14 function 1\n");
    CHECK_EQUAL(DEBUG_SCRIPT_PENDING, loaded->state);
    CHECK_EQUAL(3, loaded->op_count);
    CHECK_EQUAL(3, loaded->word_count);
    CHECK_EQUAL(0, loaded->probe_count);
    CHECK_EQUAL(DEBUG_OP_PEEK, loaded->ops[0].kind);
    CHECK_EQUAL(0x500100E8u, loaded->words[loaded->ops[0].first]);
    CHECK_EQUAL(DEBUG_OP_POKE, loaded->ops[1].kind);
    CHECK_EQUAL(0x500100B4u, loaded->words[loaded->ops[1].first]);
    CHECK_EQUAL(0x000E0002u, loaded->words[loaded->ops[1].second]);
    CHECK_EQUAL(DEBUG_OP_PIN, loaded->ops[2].kind);
    CHECK_EQUAL(14, loaded->ops[2].first);
    CHECK_EQUAL(DEBUG_PIN_FUNCTION | 1u << DEBUG_PIN_FUNCTION_SHIFT, loaded->ops[2].second);
    CHECK_EQUAL(4, loaded->ops[2].line);
}

static void a_script_with_more_words_than_it_can_keep_is_refused(void)
{
    static char text[DEBUG_SCRIPT_BYTES];
    strcpy(text, "build any\n");
    for (int index = 0; index < DEBUG_SCRIPT_WORDS; index++) {
        strcat(text, "peek 0x50010000\n");
    }
    CHECK_EQUAL(DEBUG_SCRIPT_PENDING, load(text)->state);
    CHECK_EQUAL(DEBUG_SCRIPT_WORDS, load(text)->word_count);
    strcat(text, "peek 0x50010000\n");
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_TOO_LONG, load(text)->state);
    CHECK_EQUAL(0, load(text)->op_count);
}

static void reconnect_takes_nothing(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_RECONNECT, parse("reconnect").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("reconnect now").kind);
}

static void build_names_one_tag_or_any(void)
{
    DebugCommand command = parse("build 1051212+");
    CHECK_EQUAL(DEBUG_COMMAND_BUILD, command.kind);
    CHECK(span_is(command.argument, "1051212+"));
    CHECK(debug_span_matches(parse("build any").argument, "any"));
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("build").kind);
    CHECK_EQUAL(DEBUG_COMMAND_BAD_ARGUMENT, parse("build a b").kind);
}

static void command_words_ignore_ascii_case_and_spans_match_whole_texts(void)
{
    CHECK_EQUAL(DEBUG_COMMAND_WAIT, parse("WAIT 5").kind);
    CHECK(debug_span_matches((DebugSpan){"Text size", 9}, "text SIZE"));
    CHECK(!debug_span_matches((DebugSpan){"Text", 4}, "Text size"));
    CHECK(!debug_span_matches((DebugSpan){"Text size", 9}, "Text"));
}

static void a_script_becomes_one_step_per_action(void)
{
    const DebugScriptRun *loaded =
        load("build abc1234\nplay,down:long\nwait 250\nmeasure 3000\nprofile reset\nreconnect\n");
    CHECK_EQUAL(DEBUG_SCRIPT_PENDING, loaded->state);
    CHECK_EQUAL(6, loaded->op_count);
    CHECK_EQUAL(DEBUG_OP_KEY, loaded->ops[0].kind);
    CHECK_EQUAL(KEY_PLAY, loaded->ops[0].first_length);
    CHECK_EQUAL(KEY_SCRIPT_TAP_MS, loaded->ops[0].first);
    CHECK_EQUAL(KEY_VOLUME_UP, loaded->ops[1].first_length);
    CHECK_EQUAL(KEY_SCRIPT_LONG_MS, loaded->ops[1].first);
    CHECK_EQUAL(2, loaded->ops[1].line);
    CHECK_EQUAL(DEBUG_OP_WAIT, loaded->ops[2].kind);
    CHECK_EQUAL(250, loaded->ops[2].first);
    CHECK_EQUAL(DEBUG_OP_MEASURE, loaded->ops[3].kind);
    CHECK_EQUAL(3000, loaded->ops[3].first);
    CHECK_EQUAL(DEBUG_OP_PROFILE, loaded->ops[4].kind);
    CHECK_EQUAL(DEBUG_PROFILE_RESET, loaded->ops[4].first);
    CHECK_EQUAL(DEBUG_OP_RECONNECT, loaded->ops[5].kind);
    CHECK_EQUAL(6, loaded->ops[5].line);
}

static void a_step_points_at_its_words_in_the_script(void)
{
    const DebugScriptRun *loaded = load("build any\nset Text size = Large\nscreenshot home\n");
    const DebugOp *set = &loaded->ops[0];
    const DebugOp *shot = &loaded->ops[1];
    CHECK_EQUAL(DEBUG_OP_SET, set->kind);
    CHECK(span_is((DebugSpan){loaded->text + set->first, set->first_length}, "Text size"));
    CHECK(span_is((DebugSpan){loaded->text + set->second, set->second_length}, "Large"));
    CHECK_EQUAL(DEBUG_OP_SCREENSHOT, shot->kind);
    CHECK(span_is((DebugSpan){loaded->text + shot->first, shot->first_length}, "home"));
}

static void lines_that_cannot_run_get_their_status_when_the_script_is_loaded(void)
{
    const DebugScriptRun *loaded = load("build any\n\nerase 1\nwait soon\nplay\n# done\n");
    CHECK_EQUAL(DEBUG_SCRIPT_PENDING, loaded->state);
    CHECK_EQUAL(DEBUG_LINE_OK, loaded->status[0]);
    CHECK_EQUAL(DEBUG_LINE_NOT_RUN, loaded->status[1]);
    CHECK_EQUAL(DEBUG_LINE_UNKNOWN, loaded->status[2]);
    CHECK_EQUAL(DEBUG_LINE_BAD_ARGUMENT, loaded->status[3]);
    CHECK_EQUAL(DEBUG_LINE_NOT_RUN, loaded->status[4]);
    CHECK_EQUAL(1, loaded->op_count);
    CHECK_EQUAL(5, loaded->ops[0].line);
}

static void a_script_must_name_its_build_first(void)
{
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_NO_BUILD, load("play\nbuild any\n")->state);
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_NO_BUILD, load("")->state);
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_NO_BUILD, load("# nothing\n\n")->state);
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_NO_BUILD, load("build\nplay\n")->state);
    CHECK_EQUAL(DEBUG_SCRIPT_PENDING, load("# for today\n\nbuild abc1234\nplay\n")->state);
}

static void a_script_runs_only_on_the_build_it_names_unless_it_says_any(void)
{
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_BUILD, load("build 7777777\nplay\n")->state);
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_BUILD, load("build 1.0.0\nplay\n")->state);
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_BUILD, load("build abc1234+\nplay\n")->state);
    CHECK_EQUAL(DEBUG_SCRIPT_PENDING, load("build any\nplay\n")->state);
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_BUILD, load_for("build abc1234\n", "1.0.0", false)->state);
    CHECK_EQUAL(DEBUG_SCRIPT_PENDING, load_for("build any\n", "1.0.0", false)->state);
}

static void a_refused_script_has_no_steps(void)
{
    const DebugScriptRun *loaded = load("build 7777777\nplay\nwait soon\n");
    CHECK_EQUAL(0, loaded->op_count);
    CHECK_EQUAL(DEBUG_LINE_NOT_RUN, loaded->status[0]);
    CHECK_EQUAL(DEBUG_LINE_NOT_RUN, loaded->status[2]);
}

static void no_script_runs_while_an_update_awaits_its_restart(void)
{
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_RESTART_PENDING,
                load_for("build any\nplay\n", "1.0.0-abc1234", true)->state);
}

static void a_script_past_the_size_line_or_step_limit_is_refused(void)
{
    static char text[DEBUG_SCRIPT_BYTES + 64];
    strcpy(text, "build any\n");
    for (int line = 0; line < DEBUG_SCRIPT_LINES; line++) {
        strcat(text, "#\n");
    }
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_TOO_LONG, load(text)->state);
    memset(text, '#', sizeof text - 1);
    memcpy(text, "build any\n", 10);
    text[sizeof text - 1] = '\0';
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_TOO_LONG, load(text)->state);
    strcpy(text, "build any\n");
    for (int key = 0; key < DEBUG_SCRIPT_OPS; key++) {
        strcat(text, key % 12 == 11 ? "up\n" : "up,");
    }
    strcat(text, "up\n");
    CHECK_EQUAL(DEBUG_SCRIPT_REFUSED_TOO_LONG, load(text)->state);
    CHECK_EQUAL(0, load(text)->op_count);
}

static void a_script_at_the_limits_is_accepted(void)
{
    static char text[DEBUG_SCRIPT_BYTES + 1];
    strcpy(text, "build any\n");
    for (int key = 1; key < DEBUG_SCRIPT_OPS; key++) {
        strcat(text, key % 12 == 0 ? "up\n" : "up,");
    }
    strcat(text, "up\n");
    const DebugScriptRun *loaded = load(text);
    CHECK_EQUAL(DEBUG_SCRIPT_PENDING, loaded->state);
    CHECK_EQUAL(DEBUG_SCRIPT_OPS, loaded->op_count);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(lines_are_split_at_line_feeds_and_trimmed),
        TEST_CASE(a_last_line_without_a_line_feed_counts),
        TEST_CASE(blank_lines_and_comments_are_not_commands),
        TEST_CASE(a_key_list_is_the_simulators_grammar),
        TEST_CASE(anything_else_is_unknown_and_never_guessed),
        TEST_CASE(wait_and_measure_take_milliseconds_up_to_a_minute),
        TEST_CASE(profile_takes_on_off_or_reset),
        TEST_CASE(usb_sense_takes_normal_or_kept_on),
        TEST_CASE(set_splits_the_setting_from_its_option_at_the_equals_sign),
        TEST_CASE(a_screenshot_name_is_short_lower_case_and_plain),
        TEST_CASE(peek_takes_one_word_in_hex_or_decimal),
        TEST_CASE(poke_takes_an_address_and_a_value),
        TEST_CASE(gpio_names_a_pin_and_what_to_do_with_it),
        TEST_CASE(supply_and_screen_take_off_or_on),
        TEST_CASE(standby_takes_off_or_on),
        TEST_CASE(soak_takes_one_to_180_minutes),
        TEST_CASE(a_scripts_soaks_add_their_minutes_to_its_time_limit),
        TEST_CASE(probe_lines_keep_their_words_beside_the_steps),
        TEST_CASE(rtc_read_lines_become_steps_and_other_actions_are_refused),
        TEST_CASE(a_script_with_more_words_than_it_can_keep_is_refused),
        TEST_CASE(reconnect_takes_nothing),
        TEST_CASE(build_names_one_tag_or_any),
        TEST_CASE(command_words_ignore_ascii_case_and_spans_match_whole_texts),
        TEST_CASE(a_script_becomes_one_step_per_action),
        TEST_CASE(a_step_points_at_its_words_in_the_script),
        TEST_CASE(lines_that_cannot_run_get_their_status_when_the_script_is_loaded),
        TEST_CASE(a_script_must_name_its_build_first),
        TEST_CASE(a_script_runs_only_on_the_build_it_names_unless_it_says_any),
        TEST_CASE(a_refused_script_has_no_steps),
        TEST_CASE(no_script_runs_while_an_update_awaits_its_restart),
        TEST_CASE(a_script_past_the_size_line_or_step_limit_is_refused),
        TEST_CASE(a_script_at_the_limits_is_accepted),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
