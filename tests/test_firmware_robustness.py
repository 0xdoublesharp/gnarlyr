import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import firmware
import firmware_analyze as analyze
import fuzz_campaign as campaign
import fuzz_seeds

FORK_LOG = """INFO: Running with entropic power schedule (0xFF, 100).
INFO: Seed: 1
INFO: -fork=1: fuzzing in separate process(s)
INFO: -fork=1: 40 seed inputs, starting to fuzz in /s/libFuzzerTemp.FuzzWithFork13106.dir
#7508: cov: 164 ft: 251 corp: 40 exec/s: 3754 oom/timeout/crash: 0/0/0 time: 6s job: 1 dft_time: 0
#40960: cov: 344 ft: 690 corp: 31 exec/s: 1500 oom/timeout/crash: 0/1/1 time: 20s job: 4 dft_time: 0
==10543==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000011
INFO: fuzzed for 16 seconds, wrapping up soon
INFO: exiting: 0 time: 16s
"""
COVERAGE_LOG = ("INFO: seed corpus: files: 12 min: 1b max: 96b total: 2263b rss: 42Mb\n"
                "#16\tINITED cov: 310 ft: 512 corp: 12/4096b exec/s: 0 rss: 40Mb\n"
                "#16\tDONE   cov: 310 ft: 512 corp: 12/4096b lim: 96 exec/s: 0 rss: 43Mb\n")


def elf32(functions, words):
    text = b"".join(struct.pack("<I", word) for word in words)
    names = b"\0" + b"".join(name.encode() + b"\0" for name, _ in functions)
    symbols = bytes(16)
    offset = 1
    for name, address in functions:
        symbols += struct.pack("<IIIBBH", offset, address | 1, 4, 0x12, 0, 1)
        offset += len(name) + 1
    shstrtab = b"\0.text\0.symtab\0.strtab\0.shstrtab\0"
    body = text + symbols + names + shstrtab
    header_size = 52
    positions = [header_size, header_size + len(text), header_size + len(text) + len(symbols),
                 header_size + len(text) + len(symbols) + len(names)]
    shoff = header_size + len(body)
    sections = [bytes(40),
                struct.pack("<IIIIIIIIII", 1, 1, 6, 0x1000, positions[0], len(text), 0, 0, 4, 0),
                struct.pack("<IIIIIIIIII", 7, 2, 0, 0, positions[1], len(symbols), 3, 1, 4, 16),
                struct.pack("<IIIIIIIIII", 15, 3, 0, 0, positions[2], len(names), 0, 0, 1, 0),
                struct.pack("<IIIIIIIIII", 23, 3, 0, 0, positions[3], len(shstrtab), 0, 0, 1, 0)]
    header = b"\x7fELF\x01\x01\x01" + bytes(9) + struct.pack("<HHIIIIIHHHHHH", 2, 40, 1, 0, 0, shoff, 0,
                                                              header_size, 0, 0, 40, len(sections), 4)
    return header + body + b"".join(sections)


class DictionaryTests(unittest.TestCase):
    def test_every_committed_dictionary_entry_is_valid_libfuzzer_syntax(self):
        files = sorted(firmware.FUZZ_DICTIONARIES.glob("*.dict"))
        self.assertTrue(files)
        for path in files:
            for line in path.read_text().splitlines():
                self.assertTrue(campaign.dictionary_entry_valid(line), f"{path.name}: {line}")

    def test_dictionaries_name_real_fuzzers_and_existing_files(self):
        self.assertLessEqual(set(campaign.DICTIONARIES), set(fuzz_seeds.TARGETS))
        for parts in campaign.DICTIONARIES.values():
            for part in parts:
                self.assertTrue((firmware.FUZZ_DICTIONARIES / f"{part}.dict").is_file(), part)

    def test_parts_are_merged_without_duplicates_and_run_time_entries_are_escaped(self):
        text = campaign.dictionary_text("playlist_reader", firmware.FUZZ_DICTIONARIES, ['A"\\\n'])
        lines = text.splitlines()
        self.assertEqual(len(lines), len(set(lines)))
        self.assertIn('"A\\x22\\x5C\\x0A"', lines)
        self.assertTrue(all(campaign.dictionary_entry_valid(line) for line in lines))

    def test_control_script_fuzzers_get_no_dictionary(self):
        self.assertEqual(campaign.dictionary_text("app_navigation", firmware.FUZZ_DICTIONARIES), "")

    def test_malformed_entries_are_detected(self):
        for line in ('"unterminated', 'name="\\q"', "bare", '"a"b"'):
            self.assertFalse(campaign.dictionary_entry_valid(line), line)


class SeedFramingTests(unittest.TestCase):
    def page(self, packets):
        table = b""
        body = b""
        for packet in packets:
            table += bytes([255] * (len(packet) // 255) + [len(packet) % 255])
            body += packet
        return b"OggS" + bytes(22) + bytes([len(table)]) + table + body

    def test_ogg_packets_join_lacing_segments(self):
        data = self.page([b"OpusHead" + bytes(11), b"x" * 300]) + self.page([b"y" * 3])
        self.assertEqual([len(packet) for packet in campaign.ogg_packets(data)], [19, 300, 3])

    def test_opus_seeds_skip_headers_and_carry_control_bytes(self):
        data = self.page([b"OpusHead" + bytes(11), b"OpusTags" + bytes(8), b"\xf8\xff\xfe"])
        self.assertEqual(campaign.opus_seeds(data, [bytes(12)]), [bytes(12) + b"\xf8\xff\xfe"])

    def test_harness_framing(self):
        self.assertEqual(campaign.frame_for("playlist_reader", b"a.flac\n", []),
                         [b"\xff\x00a.flac\n", b"\x00\x00a.flac\n"])
        self.assertEqual(len(campaign.frame_for("ogg_track", b"OggS", [])), 3)
        self.assertEqual(campaign.frame_for("theme_pack", b"DSTP", []), [b"DSTP"])
        self.assertEqual(campaign.frame_for("theme_pack", b"\x01DSTP", []), [])

    def test_media_routes_by_suffix_and_truncates_to_the_target_length(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "a.mp3").write_bytes(b"ID3" + bytes(100))
            (root / "notes.txt").write_bytes(b"ignored")
            paths = campaign.allowed_media([root, root / "missing"])
            self.assertEqual([path.name for path in paths], ["a.mp3"])
            seeds = campaign.media_seeds(paths, {"mp3_probe": 10, "track_tags": 1000, "cue_sheet": 5}, [])
        self.assertEqual([len(data) for data in seeds["mp3_probe"].values()], [10])
        self.assertEqual([len(data) for data in seeds["track_tags"].values()], [103])
        self.assertEqual(seeds["cue_sheet"], {})

    def test_seeding_is_idempotent_by_content(self):
        with tempfile.TemporaryDirectory() as directory:
            corpus = Path(directory) / "c"
            self.assertEqual(campaign.seed_corpus(corpus, [b"a", b"b", b"a"]), 2)
            self.assertEqual(campaign.seed_corpus(corpus, [b"a", b"b"]), 0)

    def test_smoke_seed_names_are_unchanged(self):
        inputs = fuzz_seeds.inputs("cue_sheet")
        self.assertEqual(sorted(inputs), [f"seed-{index}" for index in range(4)])
        self.assertIn("fixture-tagged.wav", fuzz_seeds.inputs("wav_source"))

    def test_synthetic_jobs_cover_every_codec_with_art_and_chapters(self):
        jobs = fuzz_seeds.ffmpeg_jobs(Path("/s"))
        outputs = {Path(job[-1]).suffix for job in jobs}
        self.assertLessEqual({".flac", ".mp3", ".ogg", ".opus", ".m4a", ".m4b", ".wav", ".aiff", ".wma", ".jpg",
                              ".png"}, outputs)
        self.assertTrue(any("attached_pic" in job for job in jobs))
        self.assertIn("[CHAPTER]", fuzz_seeds.chapter_metadata())
        self.assertNotIn("assets", fuzz_seeds.palette_only_manifest({"assets": [], "name": "x"}))


class FuzzLogTests(unittest.TestCase):
    def test_fork_log_gives_executions_rate_coverage_and_events(self):
        stats = campaign.parse_fork_log(FORK_LOG)
        self.assertEqual((stats["execs"], stats["seconds"], stats["exec_per_second"]), (40960, 20, 2048))
        self.assertEqual((stats["cov"], stats["ft"]), (344, 690))
        self.assertEqual(stats["events"], {"ooms": 0, "timeouts": 1, "crashes": 1})
        self.assertEqual(stats["sanitizer"], ["AddressSanitizer heap-buffer-overflow"])

    def test_coverage_pass_reads_the_inited_line_and_an_empty_corpus(self):
        self.assertEqual(campaign.parse_coverage(COVERAGE_LOG), {"cov": 310, "ft": 512, "files": 12})
        self.assertEqual(campaign.parse_coverage("#1\tINITED exec/s: 0 rss: 30Mb\n"), {"cov": 0, "ft": 0,
                                                                                         "files": 0})
        self.assertIsNone(campaign.parse_coverage("nothing"))

    def test_fork_command_keeps_going_past_crashes_and_uses_the_dictionary(self):
        command = campaign.fork_command("/b/fuzz_x", "/c", "/r", {"seconds": 60, "max_length": 99, "seed": 3,
                                                                  "dictionary": "/d.dict"})
        for flag in ("-fork=1", "-ignore_crashes=1", "-ignore_timeouts=1", "-max_total_time=60", "-max_len=99",
                     "-seed=3", "-dict=/d.dict", "-artifact_prefix=/r/"):
            self.assertIn(flag, command)

    def test_leak_detection_is_enabled_without_dropping_other_options(self):
        env = campaign.sanitizer_env({"ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1"}, "/s.txt")
        self.assertEqual(env["ASAN_OPTIONS"], "abort_on_error=1:detect_leaks=1")
        self.assertEqual(env["LSAN_OPTIONS"], "suppressions=/s.txt:print_suppressions=0")

    def test_the_committed_leak_suppressions_name_only_libfuzzer_internals(self):
        lines = (firmware.SOURCE / "tests/fuzz/lsan-suppressions.txt").read_text().splitlines()
        self.assertEqual(lines, ["leak:fuzzer::StartRssThread", "leak:__thread_struct"])

    def test_duration(self):
        self.assertEqual(campaign.duration_seconds(None, None), 600)
        self.assertEqual(campaign.duration_seconds(2.5, None), 150)
        self.assertEqual(campaign.duration_seconds(None, 1), 3600)


class CampaignTests(unittest.TestCase):
    def runner(self, command, log, env):
        text = COVERAGE_LOG
        if "-fork=1" in command:
            artifacts = Path(next(part for part in command if part.startswith("-artifact_prefix="))[17:])
            (artifacts / "crash-abc").write_bytes(b"boom")
            (Path(command[1]) / "new-input").write_bytes(b"n")
            text = FORK_LOG
        elif "-merge=1" in command:
            (Path(command[-2]) / "kept").write_bytes(b"k")
        self.commands.append(command)
        with open(log, "a") as handle:
            handle.write(text)
        return 0

    def test_a_target_is_seeded_fuzzed_merged_and_reported_with_a_repro_line(self):
        self.commands = []
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            layout = {"corpus": root / "corpus", "report": root / "report", "summary": "summary-1",
                      "binaries": root / "bin"}
            run = campaign.Campaign(layout, runner=self.runner)
            settings = {"seconds": 5, "max_length": 64, "seed": None, "dictionary": None, "seeds": [b"s1", b"s2"]}
            results = run.run(["cue_sheet"], lambda name: settings, {}, 2)
            result = results[0]
            self.assertEqual(result["seeds_added"], 2)
            self.assertEqual(result["files_before"], 2)
            self.assertEqual(sorted(path.name for path in (root / "corpus/cue_sheet").iterdir()), ["kept"])
            self.assertEqual(result["execs"], 40960)
            self.assertEqual([artifact["kind"] for artifact in result["artifacts"]], ["crash"])
            repro = (root / "report/cue_sheet/crash-abc.repro").read_text()
            self.assertIn("fuzz_cue_sheet -runs=1", repro)
            self.assertIn("detect_leaks=1", repro)
            summary = json.loads((root / "report/summary-1.json").read_text())
            self.assertEqual(summary["cue_sheet"]["coverage_after"]["cov"], 310)
            self.assertIn("crash-abc", (root / "report/summary-1.md").read_text())
        self.assertEqual([("-fork=1" in c, "-merge=1" in c) for c in self.commands],
                         [(False, False), (True, False), (False, True), (False, False)])

    def test_a_failed_merge_keeps_the_existing_corpus(self):
        self.commands = []

        def failing(command, log, env):
            code = self.runner(command, log, env)
            return 1 if "-merge=1" in command else code

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            layout = {"corpus": root / "corpus", "report": root / "report", "summary": "s", "binaries": root}
            settings = {"seconds": 5, "max_length": 64, "seeds": [b"s1"]}
            campaign.Campaign(layout, runner=failing).run(["store"], lambda name: settings, {}, 1)
            self.assertIn("seed-", " ".join(path.name for path in (root / "corpus/store").iterdir()))
            self.assertFalse((root / "corpus/store.merging").exists())

    def test_sanitizer_reports_without_an_input_are_listed(self):
        result = {"seconds": 1, "execs": 2, "exec_per_second": 2, "coverage_before": None, "coverage_after": None,
                  "files_before": 1, "files_after": 1, "artifacts": [], "log": "/r/x/fuzz.log",
                  "sanitizer": ["LeakSanitizer detected"]}
        text = campaign.summary_markdown({"x": result, "y": {"error": "boom", "artifacts": []}})
        self.assertIn("LeakSanitizer detected without leaving an input", text)
        self.assertIn("| y | error: boom", text)

    def test_a_crashing_seed_keeps_the_corpus_unmerged(self):
        self.commands = []

        def crashing(command, log, env):
            self.commands.append(command)
            with open(log, "a") as handle:
                handle.write("==1== ERROR: libFuzzer: deadly signal\n")
            return 77

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            layout = {"corpus": root / "corpus", "report": root / "report", "summary": "s", "binaries": root}
            settings = {"seconds": 5, "max_length": 64, "seeds": [b"s1", b"s2"]}
            result = campaign.Campaign(layout, runner=crashing).run(["ape_track"], lambda name: settings, {}, 1)[0]
            self.assertIsNone(result["merge_exit_code"])
            self.assertEqual(result["files_after"], 2)
        self.assertFalse(any("-merge=1" in command for command in self.commands))

    def test_artifacts_from_an_earlier_run_the_same_day_are_not_reported_again(self):
        self.commands = []
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "report/store").mkdir(parents=True)
            (root / "report/store/crash-old").write_bytes(b"x")
            layout = {"corpus": root / "corpus", "report": root / "report", "summary": "s", "binaries": root}
            settings = {"seconds": 5, "max_length": 64, "seeds": [b"s1"]}
            result = campaign.Campaign(layout, runner=self.runner).run(["store"], lambda name: settings, {}, 1)[0]
        self.assertEqual([Path(item["path"]).name for item in result["artifacts"]], ["crash-abc"])

    def test_two_campaigns_cannot_share_a_corpus(self):
        with tempfile.TemporaryDirectory() as directory:
            with campaign.corpus_lock(Path(directory), "store"):
                with self.assertRaisesRegex(RuntimeError, "another campaign"):
                    with campaign.corpus_lock(Path(directory), "store"):
                        pass


class FuzzArgumentTests(unittest.TestCase):
    def parse(self, *argv):
        with mock.patch.object(sys, "argv", ["firmware.py", *argv]):
            return firmware.arguments()[1]

    def rejected(self, *argv):
        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            self.parse(*argv)

    def test_long_mode_options(self):
        args = self.parse("fuzz", "--long", "--hours", "2", "--target", "cue_sheet")
        self.assertEqual((args.long, args.hours, args.target), (True, 2.0, ["cue_sheet"]))
        self.rejected("fuzz", "--minutes", "5")
        self.rejected("fuzz", "--long", "--runs", "10")
        self.rejected("fuzz", "--long", "--minutes", "1", "--hours", "1")
        self.rejected("fuzz", "--long", "--minutes", "0")
        self.rejected("build", "--long")

    def test_smoke_mode_is_unchanged(self):
        args = self.parse("fuzz", "--runs", "1000", "--seed", "1", "--jobs", "4")
        self.assertEqual((args.long, args.runs, args.seed), (False, 1000, 1))

    def test_analyze_parts(self):
        self.assertEqual(self.parse("analyze", "--part", "stack").part, ["stack"])
        self.rejected("build", "--part", "stack")
        self.rejected("analyze", "--part", "lint")


class SourceAnalysisTests(unittest.TestCase):
    source = Path("/repo/firmware")

    def test_units_exclude_third_party_tests_and_duplicates(self):
        database = [{"directory": "/repo/firmware/build", "file": f"/repo/firmware/{name}", "command": "cc -c x"}
                    for name in ("src/a.c", "src/a.c", "third_party/opus/b.c", "tests/t.c",
                                 "platform/device/logic/c.c", "platform/device/board/d.c")]
        with mock.patch.object(analyze.Path, "resolve", lambda self: self):
            names = [unit["name"] for unit in analyze.selected_units(database, self.source)]
        self.assertEqual(names, ["platform/device/logic/c.c", "src/a.c"])

    def test_tidy_output_is_split_per_check_and_skips_third_party(self):
        output = ("/repo/firmware/src/a.c:3:5: warning: bad thing [bugprone-x,cert-y]\n  3 | code\n"
                  "/repo/firmware/third_party/z.c:1:1: warning: other [bugprone-x]\n")
        findings = analyze.parse_tidy(output, self.source)
        self.assertEqual([(item["path"], item["check"], item["line"]) for item in findings],
                         [("src/a.c", "bugprone-x", 3), ("src/a.c", "cert-y", 3)])

    def test_analyzer_command_drops_sanitizers_and_outputs(self):
        unit = {"file": "/r/a.c", "path": "/r/a.c",
                "command": "cc -DX -fsanitize=undefined -Werror -I/i -o a.o -c /r/a.c"}
        command = analyze.analyzer_command("clang", unit, Path("/o.sarif"))
        self.assertIn("--analyze", command)
        self.assertIn("-analyzer-checker=security.ArrayBound", command)
        self.assertNotIn("-fsanitize=undefined", command)
        self.assertNotIn("a.o", command)
        self.assertEqual(command[-3:], ["-DX", "-I/i", "/r/a.c"])

    def test_sarif_results(self):
        document = {"runs": [{"results": [{"ruleId": "core.NullDereference", "level": "warning",
                                           "message": {"text": "Null"},
                                           "locations": [{"physicalLocation": {
                                               "artifactLocation": {"uri": "file:///repo/firmware/src/a.c"},
                                               "region": {"startLine": 7, "startColumn": 2}}}]}]}]}
        findings = analyze.parse_sarif(document, self.source)
        self.assertEqual((findings[0]["path"], findings[0]["line"], findings[0]["check"]),
                         ("src/a.c", 7, "core.NullDereference"))

    def test_fingerprints_survive_line_moves_and_number_changes(self):
        first = {"tool": "t", "check": "c", "path": "p", "message": "index 3 past 4", "line": 10}
        moved = {**first, "line": 40, "message": "index 5 past 6"}
        self.assertEqual(analyze.fingerprint(first, "a[i] = 1;"), analyze.fingerprint(moved, "a[i]  =  1;"))
        self.assertNotEqual(analyze.fingerprint(first, "a[i] = 1;"), analyze.fingerprint(first, "a[j] = 1;"))

    def test_baseline_requires_a_reason_and_reports_new_and_stale(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "baseline.tsv"
            path.write_text("fingerprint\ttool\tcheck\tpath\treason\n" + "a" * 16 + "\tt\tc\tp\twhy\n")
            baseline = analyze.read_baseline(path)
            path.write_text("b" * 16 + "\tt\tc\tp\t\n")
            with self.assertRaisesRegex(ValueError, "reason"):
                analyze.read_baseline(path)
        new, stale = analyze.compare([{"fingerprint": "c" * 16}], baseline)
        self.assertEqual(([item["fingerprint"] for item in new], stale), (["c" * 16], ["a" * 16]))
        self.assertEqual(analyze.compare([], baseline, "clang-analyzer")[1], [])
        self.assertEqual(analyze.compare([], baseline, "t")[1], ["a" * 16])

    def test_the_committed_baseline_parses(self):
        analyze.read_baseline(firmware.ANALYSIS / "baseline.tsv")


class StackBoundTests(unittest.TestCase):
    CI = """graph: { title: "a.c"
node: { title: "main" label: "main\\na.c:3:5\\n16 bytes (static)\\n0 dynamic objects" }
node: { title: "work" label: "work\\na.c:4:5\\n40 bytes (static)\\n0 dynamic objects" }
node: { title: "leaf" label: "leaf\\na.c:5:5\\n8 bytes (dynamic,bounded)\\n1 dynamic objects" }
node: { title: "memcpy" label: "memcpy\\na.c:1:1" shape : ellipse }
node: { title: "__indirect_call" label: "Indirect Call Placeholder" shape : ellipse }
node: { title: "ping" label: "ping\\na.c:6:5\\n24 bytes (static)\\n0 dynamic objects" }
node: { title: "pong" label: "pong\\na.c:7:5\\n24 bytes (static)\\n0 dynamic objects" }
node: { title: "tick" label: "tick\\na.c:8:5\\n12 bytes (static)\\n0 dynamic objects" }
edge: { sourcename: "main" targetname: "work" label: "a.c:3:9" }
edge: { sourcename: "work" targetname: "leaf" label: "a.c:4:9" }
edge: { sourcename: "work" targetname: "memcpy" label: "a.c:4:20" }
edge: { sourcename: "main" targetname: "__indirect_call" label: "a.c:3:30" }
edge: { sourcename: "ping" targetname: "pong" label: "a.c:6:9" }
edge: { sourcename: "pong" targetname: "ping" label: "a.c:7:9" }
}
"""

    def graph(self):
        graph = analyze.CallGraph()
        graph.add(*analyze.parse_callgraph(self.CI, "a.ci"))
        graph.finish()
        return graph

    def test_callgraph_nodes_and_edges(self):
        nodes, edges = analyze.parse_callgraph(self.CI, "a.ci")
        self.assertEqual(nodes["work"]["frame"], 40)
        self.assertEqual(nodes["leaf"]["kind"], "dynamic,bounded")
        self.assertIsNone(nodes["memcpy"]["frame"])
        self.assertIn(("main", "__indirect_call", "a.c:3:30"), edges)

    def test_depth_follows_the_deepest_direct_path(self):
        depths = self.graph().depths()
        self.assertEqual(depths["main"], (64, ("main", "work", "leaf")))

    def test_indirect_calls_add_the_given_cost(self):
        depths = self.graph().depths(100, ("ping",))
        self.assertEqual(depths["main"][0], 116)

    def test_an_edge_between_two_separate_self_loops_is_followed(self):
        graph = analyze.CallGraph()
        graph.frames = {"a": 10, "b": 20, "c": 30}
        graph.calls = {"a": {"a", "b"}, "b": {"b", "c"}, "c": set()}
        self.assertEqual(graph.cycles(), [["a"], ["b"]])
        self.assertEqual(graph.depths()["a"], (60, ("a", "b", "c")))

    def test_recursion_unknown_and_dynamic_frames_are_flagged(self):
        graph = self.graph()
        self.assertEqual(graph.cycles(), [["ping", "pong"]])
        self.assertEqual(graph.unknown, {"memcpy"})
        self.assertEqual(graph.dynamic, {"leaf"})
        self.assertEqual(graph.sites, {"main": ["a.c:3:30"]})

    def test_bound_report_uses_the_deepest_address_taken_function_for_indirect_calls(self):
        report, worst = analyze.bound_report(self.graph(), {"main": "main", "systick": "tick", "x": "nope"},
                                             ["work", "tick"])
        self.assertEqual(worst, "work")
        self.assertEqual(report["main"]["direct_bytes"], 64)
        self.assertEqual(report["main"]["indirect_bytes"], 64)
        self.assertEqual(report["main"]["indirect_sites_on_path"], 1)
        self.assertTrue(report["x"]["missing"])

    def test_address_taken_functions_are_found_in_loaded_words(self):
        data = elf32([("main", 0x1000), ("callback", 0x1008), ("alias", 0x1008), ("unused", 0x100C)],
                     [0, 0x1009, 0x2000, 7])
        taken, present = analyze.address_taken(data)
        self.assertEqual(taken, ["alias", "callback"])
        self.assertEqual(present, ["alias", "callback", "main", "unused"])

    def test_folded_aliases_take_the_frame_of_the_function_they_share_an_address_with(self):
        graph = analyze.CallGraph()
        graph.frames = {"get.lto_priv.0": 24}
        graph.calls = {"get.lto_priv.0": {"leaf"}}
        analyze.resolve_aliases(graph, [{"get.lto_priv.0", "get.lto_priv.1"}, {"lonely"}])
        self.assertEqual(graph.frames, {"get.lto_priv.0": 24, "get.lto_priv.1": 24})
        self.assertEqual(graph.calls["get.lto_priv.1"], {"leaf"})

    def test_discarded_archive_members_are_not_linked_objects(self):
        text = ("Discarded input sections\n .text.x 0x0 0x4 ../src/libecho_codec.a(flac.c.obj)\n"
                "Linker script and memory map\n .text.app_step\n                0x03050000 0x40 "
                "../src/libecho_app.a(app.c.obj)\n")
        objects = analyze.linked_objects(text, Path("/b/platform"))
        self.assertEqual(objects, {("/b/src/libecho_app.a", "app.c.obj"): {"app_step"}})

    def test_cold_and_cloned_function_sections_name_their_function(self):
        text = (" .text.unlikely.seek_to.constprop.0 0x1000 0x8 CMakeFiles/x.dir/a.c.obj\n")
        names = analyze.linked_objects(text, Path("/b"))["/b/CMakeFiles/x.dir/a.c.obj"]
        self.assertIn("seek_to.constprop.0", names)

    def test_program_graph_joins_link_time_and_per_file_graphs_for_kept_functions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            program = root / "platform/device"
            library = root / "src/codec/CMakeFiles/echo_codec.dir"
            program.mkdir(parents=True)
            library.mkdir(parents=True)
            (program / "prog.elf").write_bytes(elf32([("main", 0x1000), ("decode", 0x1008)], [0]))
            (program / "prog.elf.ltrans0.ltrans.ci").write_text(
                'node: { title: "/tmp/cc.ltrans0.o:main" label: "main\\na.c:1:1\\n16 bytes (static)\\n'
                '0 dynamic objects" }\n'
                'edge: { sourcename: "/tmp/cc.ltrans0.o:main" targetname: "decode" label: "a.c:2:3" }\n')
            (library / "flac.c.ci").write_text(
                'node: { title: "decode" label: "decode\\nflac.c:1:1\\n40 bytes (static)\\n0 dynamic objects" }\n'
                'node: { title: "main" label: "main\\nflac.c:9:1\\n900 bytes (static)\\n0 dynamic objects" }\n')
            (program / "prog.map").write_text(
                "Linker script and memory map\n .text.unlikely.decode\n                0x1008 0x8 "
                "../../src/codec/libecho_codec.a(flac.c.obj)\n")
            graph, files = analyze.program_graph(program, "prog", set())
        self.assertEqual(len(files), 2)
        self.assertEqual(graph.frames, {"main": 16, "decode": 40})
        self.assertEqual(graph.depths()["main"], (56, ("main", "decode")))

    def test_map_objects_include_archive_members(self):
        text = (" .text.main      0x1000 0x8 CMakeFiles/x.dir/a.c.obj\n"
                " .text.helper    0x1008 0x4 ../src/libecho_app.a(app.c.obj)\n")
        objects = analyze.linked_objects(text, Path("/b/platform"))
        self.assertEqual(objects["/b/platform/CMakeFiles/x.dir/a.c.obj"], {"main"})
        self.assertEqual(objects[("/b/src/libecho_app.a", "app.c.obj")], {"helper"})

    def test_object_and_callgraph_names_follow_gcc_auxiliary_naming(self):
        self.assertEqual(analyze.callgraph_of("/b/x.dir/a.c.obj"), "/b/x.dir/a.c.ci")
        self.assertEqual(analyze.callgraph_of("/b/x.dir/a.c.o"), "/b/x.dir/a.c.ci")
        self.assertEqual(analyze.object_of("/b/a.c.ci", {"/b/a.c.obj"}), "/b/a.c.obj")
        self.assertIsNone(analyze.object_of("/b/a.c.ci", set()))

    def test_link_time_and_per_file_static_names_match_the_symbol_table(self):
        self.assertEqual(analyze.function_name("/tmp/cc1.ltrans0.o:tag_is"), "tag_is")
        self.assertEqual(analyze.function_name("/src/audio.c:close_file"), "close_file")
        self.assertEqual(analyze.function_name("save.constprop.0"), "save.constprop.0")

    def test_vector_table_entries_are_not_indirect_call_targets(self):
        report, worst = analyze.bound_report(self.graph(), {"main": "main"}, ["main", "device_fault_entry", "tick"])
        self.assertEqual(worst, "tick")
        self.assertEqual(report["main"]["indirect_bytes"], 16 + 48)

    def test_combined_figure_adds_exception_frames(self):
        report = {"main": {"direct_bytes": 100}, "systick": {"direct_bytes": 40}, "fault": {"direct_bytes": 8}}
        self.assertEqual(analyze.combine(report, "direct_bytes"), 100 + 32 + 40 + 32 + 8)
        self.assertEqual(analyze.combine({"main": {"direct_bytes": 1}}, "direct_bytes"), 33)

    def test_named_symbols_are_read_from_the_elf(self):
        data = elf32([("main", 0x1000), ("core1_bss_end", 0x0102A000)], [0])
        self.assertEqual(analyze.elf_symbol(data, "core1_bss_end"), 0x0102A001)
        self.assertIsNone(analyze.elf_symbol(data, "missing"))


if __name__ == "__main__":
    unittest.main()
