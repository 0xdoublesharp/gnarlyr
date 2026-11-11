import json
import re
import struct

import echo_language

ASCII_LOWER = echo_language.ASCII_LOWER
FIELDS = {"VERSION_FIELD": 4, "BRIGHTNESS_FIELD": 6, "TIMEOUT_FIELD": 7,
          "TEXT_SIZE_FIELD": 24, "ORIENTATION_FIELD": 26, "HALF_DB_CHECKSUM_FIELD": 38}
BOUNDS = {6: (1, 10), 7: (0, 6), 8: (0, 1), 9: (0, 2), 10: (0, 6),
          11: (0, 2), 12: (0, 2), 13: (0, 60), 14: (0, 3), 15: (0, 1),
          16: (0, 60), 17: (0, 7), 18: (0, 7), 24: (0, 2), 25: (0, 2),
          26: (0, 2), 27: (0, 4)}
SPECS = (
    ("text_size", "STR_TEXT_SIZE", "TEXT_SIZE_OPTIONS", ("small", "medium", "large"),
     ("STR_SIZE_SMALL", "STR_SIZE_MEDIUM", "STR_SIZE_LARGE"), 24, 255, 0, 0),
    ("orientation", "STR_ORIENTATION", "ORIENTATION_OPTIONS",
     ("landscape", "portrait", "portrait_flipped"),
     ("STR_LANDSCAPE", "STR_PORTRAIT", "STR_PORTRAIT_FLIPPED"), 26, 255, 0, 0),
    ("brightness", "STR_BRIGHTNESS", "BRIGHTNESS_OPTIONS", tuple(str(index) for index in range(1, 11)),
     (), 6, 255, 0, 1),
    ("screen_timeout", "STR_SCREEN_TIMEOUT", "TIMEOUT_OPTIONS",
     ("10s", "20s", "30s", "1m", "2m", "5m", "never"),
     ("STR_SECONDS_10", "STR_SECONDS_20", "STR_SECONDS_30", "STR_MINUTES_1",
      "STR_MINUTES_2", "STR_MINUTES_5", "STR_NEVER"), 7, 255, 0, 0),
    ("replay_gain", "STR_REPLAY_GAIN", "REPLAY_GAIN_OPTIONS", ("off", "track", "album"),
     ("STR_OFF", "STR_REPLAY_GAIN_TRACK", "STR_REPLAY_GAIN_ALBUM"), 5, 3, 0, 0),
    ("prevent_clipping", "STR_PREVENT_CLIPPING", "CLIPPING_OPTIONS", ("off", "on"),
     ("STR_OFF", "STR_ON"), 5, 4, 2, 0),
)


def array_source(source, name):
    match = re.search(r"\b" + name + r"\s*\[[^]]*\]\s*=\s*\{([^}]+)\}", source)
    if match is None:
        raise ValueError("the installed setting options are unknown")
    return match.group(1)


class Layout:
    def __init__(self, snapshot):
        self.snapshot = snapshot
        source = snapshot.show("firmware/src/settings/settings.c")
        self.ui = snapshot.show("firmware/src/app/settings_screens.c")
        if snapshot.settings_version not in (13, 14, 15):
            raise ValueError("only the declared version 13 to 15 settings layouts are supported")
        if any(echo_language.constant(source, name) != value for name, value in FIELDS.items()):
            raise ValueError("the installed settings fields have changed")
        header = snapshot.show("firmware/src/settings/include/settings/settings.h")
        if (echo_language.constant(header, "SETTINGS_BRIGHTNESS_MIN") != 1 or
                echo_language.constant(header, "SETTINGS_BRIGHTNESS_MAX") != 10):
            raise ValueError("the installed brightness range has changed")
        for name, expected in (("TextSize", ["TEXT_SIZE_SMALL", "TEXT_SIZE_MEDIUM", "TEXT_SIZE_LARGE"]),
                               ("Orientation", ["ORIENTATION_LANDSCAPE", "ORIENTATION_PORTRAIT", "ORIENTATION_PORTRAIT_FLIPPED"])):
            declaration = re.search(r"typedef\s+enum\s+" + name + r"\s*\{([^}]+)\}", header)
            members = [value.strip() for value in declaration.group(1).split(",")] if declaration else []
            if members != expected:
                raise ValueError("the installed display setting encoding has changed")
        self.specs = SPECS[:4] if snapshot.settings_version == 13 else SPECS
        self.title_ids()
        for spec in self.specs:
            body = array_source(self.ui, spec[2])
            actual = tuple(re.findall(r"\bSTR_[A-Z0-9_]+\b", body)) if spec[4] else tuple(re.findall(r'"([0-9]+)"', body))
            if actual != (spec[4] or spec[3]) or not re.search(r"\{\s*" + spec[1] + r"\s*,", self.ui):
                raise ValueError("the installed settings choices have changed")
        if snapshot.settings_version >= 14:
            compact = re.sub(r"\s+", "", source)
            encodings = ("bytes[VERSION_FIELD+1]=settings->replay_gain_mode|(settings->prevent_clipping?4u:0u);",
                         "bytes[VERSION_FIELD+1]=settings->replay_gain_mode|(settings->prevent_clipping?4u:0u)|"
                         "(settings->battery_percent?8u:0u);")
            if not any(encoding in compact for encoding in encodings):
                raise ValueError("the installed ReplayGain settings encoding has changed")

    def title_ids(self):
        return self.snapshot.setting_title_ids()

    def value(self, spec, data):
        index = ((data[spec[5]] & spec[6]) >> spec[7]) - spec[8]
        return spec[3][index]

    def changed(self, spec, value, data):
        changed = bytearray(data)
        raw = (spec[3].index(value) + spec[8]) << spec[7]
        changed[spec[5]] = changed[spec[5]] & (255 ^ spec[6]) | raw
        field = self.snapshot.settings_checksum_field(changed[4])
        struct.pack_into("<I", changed, field, self.snapshot.format.fnv(changed[:field]))
        return bytes(changed)


class Manager:
    def __init__(self, tool, api):
        self.tool = tool
        self.api = api
        self.language = echo_language.Manager(tool, api)

    def error(self, code, message):
        raise self.api.DebugError(code, message)

    def layout(self, context):
        try:
            return Layout(context["snapshot"])
        except ValueError as error:
            self.error("settings_layout_unknown", str(error))

    def read(self, context):
        self.language.selected(context)
        try:
            data = echo_language.read_file(context["internal"], (self.api.DATA_DIRECTORY, "settings.bin"), 4096)
        except FileNotFoundError:
            self.error("settings_missing", "save settings on the player before companion control")
        snapshot = context["snapshot"]
        if not snapshot.settings_checksum_valid(data):
            self.error("settings_invalid", "the saved settings header or checksum is invalid")
        if data[4] != snapshot.settings_version:
            self.error("settings_migration_required", "save settings with the installed firmware before companion control")
        if (not snapshot.settings_flags_valid(data[4], data[5]) or
                any(not low <= data[offset] <= high for offset, (low, high) in BOUNDS.items()) or
                any(value > 40 for value in data[28:38]) or not snapshot.settings_extension_valid(data)):
            self.error("settings_invalid", "the saved settings contain values the firmware would reject")
        return data

    def texts(self, context):
        entries = {entry["code"]: entry for entry in self.language.packs(context)}
        active = self.language.selected(context)
        active = active if active in entries else "en"
        return active, entries[active]["texts"]

    def describe(self, spec, data, texts, layout):
        label = texts[spec[1]]
        options = [texts[name] for name in spec[4]] if spec[4] else list(spec[3])
        return {"key": spec[0], "label": label, "value": layout.value(spec, data),
                "options": [{"value": value, "label": option} for value, option in zip(spec[3], options)]}

    def list(self, music_volume=None):
        context = self.language.context(music_volume)
        layout = self.layout(context)
        data = self.read(context)
        language, texts = self.texts(context)
        return {"ok": True, **context["snapshot"].metadata(), "language": language,
                "source": "saved_settings", "settings_version": data[4],
                "settings": [self.describe(spec, data, texts, layout) for spec in layout.specs]}

    def script(self, context, layout, spec, value):
        _, texts = self.texts(context)
        label = self.language.safe_script_text(texts[spec[1]], label=True)
        titles = [texts[name].translate(ASCII_LOWER) for name in layout.title_ids()]
        options = [texts[name] for name in spec[4]] if spec[4] else list(spec[3])
        if (titles.count(label.translate(ASCII_LOWER)) != 1 or
                len({option.translate(ASCII_LOWER) for option in options}) != len(options)):
            self.error("settings_choices_ambiguous", "the localized settings labels are ambiguous")
        option = self.language.safe_script_text(options[spec[3].index(value)])
        text = "build " + context["snapshot"].build + "\nset " + label + "=" + option + "\nwait 250\nreconnect\n"
        return self.api.prepare_script(text, context["snapshot"].build).decode("utf-8")

    def set(self, setting, value, music_volume=None, timeout=420.0):
        if not isinstance(setting, str) or not isinstance(value, str):
            self.error("usage", "setting and value must be canonical strings from settings-list")
        self.language.timeout(timeout)
        context = self.language.context(music_volume)
        layout = self.layout(context)
        spec = next((item for item in layout.specs if item[0] == setting), None)
        if spec is None or value not in spec[3]:
            self.error("settings_option_unknown", "the installed build does not offer this setting or value")
        before = self.read(context)
        previous = layout.value(spec, before)
        result = {"ok": True, **context["snapshot"].metadata(), "setting": setting,
                  "before": previous, "value": value, "verified": True, "changed": previous != value}
        if previous == value:
            return result
        script = self.script(context, layout, spec, value)
        newer = self.language.recheck(context)
        if self.read(newer) != before:
            self.error("settings_conflict", "the saved settings changed before the command was sent")
        runner = self.api.Tool(host=self.tool.host, volume_name=self.tool.volume_name,
                               sleep=self.tool.sleep, clock=self.tool.clock, wall=self.tool.wall)
        ran = runner.run_text(script, timeout)
        after = self.read(self.language.recheck(context))
        expected = layout.changed(spec, value, before)
        if after == before:
            self.error("settings_not_applied", "the returned settings do not confirm the requested change")
        if after != expected:
            differences = [{"offset": index, "expected": expected[index], "observed": after[index]}
                           for index in range(min(len(after), len(expected))) if after[index] != expected[index]]
            self.error("settings_state_changed", "unexpected saved settings differences: " +
                       json.dumps(differences, separators=(",", ":")) +
                       ("; file length changed" if len(after) != len(expected) else ""))
        return {**result, "run": ran}
