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
    const DebugScriptRun *loaded = load("build any\n\npoke 1\nwait soon\nplay\n# done\n");
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
