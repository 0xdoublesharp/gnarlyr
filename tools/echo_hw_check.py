#!/usr/bin/env python3
"""Check the player's debug report and run automated on-player hardware validation."""

import argparse
import fcntl
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import echo_debug


SCRATCH_ROOT = Path("/Volumes/Scratch/SnowskyEcho")
ARM_ADDR2LINE_FALLBACK = SCRATCH_ROOT / "tools/arm-15.3-expanded/Payload/bin/arm-none-eabi-addr2line"
OVERLAY_RANGE = range(0x0307A000, 0x03092000)
RESIDENT_RANGE = range(0x03050000, 0x0307A000)
VALID_RATES = (44100, 48000, 88200, 96000, 176400, 192000)
VALID_BITS = (0, 16, 24, 32)
STORAGE_TEST_BYTES = 1024 * 1024
STORAGE_PLAYBOOK_BYTES = 8 * 1024 * 1024
STORAGE_BURST_BYTES = 256 * 1024
STORAGE_BURST_COUNT = 4
STORAGE_TEST_FILE = "xfer.tmp"
PLAYBOOK_REPORT_DIR = SCRATCH_ROOT / "reports" / "hw-playbooks"

DAC_TEST_SCRIPT = """\
reconnect dac
"""

LOCAL_SMOKE_SCRIPT = """\
open home
wait 300
open coverflow
wait 300
open home
wait 300
reconnect
"""

PLAYBOOK_UI_SCRIPT = """\
profile reset
open home
wait 300
rtc read
screenshot home
profile on
measure 2000
open coverflow
wait 500
down
wait 600
down
wait 600
down
wait 600
up
wait 600
screenshot coverflow
measure 2000
profile off
open home
reconnect
"""

PLAYBOOK_PLAYBACK_SCRIPT = """\
profile reset
open nowplaying
wait 400
play
wait 600
profile on
measure 3000
screen off
wait 500
measure 3000
screen on
wait 300
profile off
play
wait 300
open home
reconnect
"""

PLAYBOOK_RESCAN_SCRIPT = """\
profile reset
open home
wait 300
profile on
rescan check
wait 6000
profile off
reconnect
"""

PLAYBOOK_NAMES = ("storage", "codecs", "settings", "ui", "playback", "rescan", "dac", "all", "certify")

AUDIO_EXTENSIONS = {
    ".wav", ".flac", ".mp3", ".m4a", ".aac", ".ogg", ".opus", ".aiff", ".aif",
    ".wma", ".ape", ".dsf", ".dff",
}

SETTINGS_LABELS = {
    "gain": ("Low", "High"),
    "dac_filter": (
        "Fast, low latency",
        "Fast, phase compensated",
        "Slow, low latency",
        "Slow, phase compensated",
        "Non-oversampling",
    ),
    "equalizer": ("Off", "Retro", "Bass", "Heavy", "Pop", "Jazz", "Unique", "Custom"),
    "crossfeed": ("Off", "Low", "Medium", "High"),
    "replay_gain": ("Off", "Track", "Album", "Auto"),
    "prevent_clipping": ("Off", "On"),
    "skip_silence": ("Off", "On"),
    "speech_boost": ("Off", "On"),
    "playback_speed": (
        "0.5×", "0.75×", "Normal (1×)", "1.25×", "1.5×", "1.75×", "2×", "2.5×", "3×",
    ),
    "now_playing_style": ("Cover", "Cassette", "VU meters", "Spectrum"),
    "orientation": ("Landscape", "Portrait", "Portrait, flipped"),
    "text_size": ("Small", "Medium", "Large"),
}

SETTINGS_DISPLAY_NAMES = {
    "gain": "Gain",
    "dac_filter": "DAC filter",
    "equalizer": "Equalizer",
    "crossfeed": "Crossfeed",
    "replay_gain": "ReplayGain",
    "prevent_clipping": "Prevent clipping",
    "skip_silence": "Skip silence",
    "speech_boost": "Speech boost",
    "playback_speed": "Playback speed",
    "now_playing_style": "Now Playing style",
    "orientation": "Orientation",
    "text_size": "Text size",
}

COREAUDIO_STREAM_SWIFT = r"""
import AudioToolbox
import CoreAudio
import Foundation

let args = CommandLine.arguments
let names = args.count > 1 ? args[1].split(separator: "|").map(String.init) : []
let rate = args.count > 2 ? (Double(args[2]) ?? 96000.0) : 96000.0
let wantedBits: UInt32 = args.count > 3 ? (UInt32(args[3]) ?? 0) : 0
let amplitude: Float = args.count > 4 ? (Float(args[4]) ?? 0.008) : 0.008
let connectTimeout = args.count > 5 ? (Double(args[5]) ?? 15.0) : 15.0
let streamTimeout = args.count > 6 ? (Double(args[6]) ?? 20.0) : 20.0

func allDevices() -> [AudioDeviceID] {
    var addr = AudioObjectPropertyAddress(
        mSelector: kAudioHardwarePropertyDevices,
        mScope: kAudioObjectPropertyScopeGlobal,
        mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size) == noErr else {
        return []
    }
    let count = Int(size) / MemoryLayout<AudioDeviceID>.size
    var ids = [AudioDeviceID](repeating: 0, count: count)
    guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &ids) == noErr else {
        return []
    }
    return ids
}

func deviceName(_ id: AudioDeviceID) -> String {
    var addr = AudioObjectPropertyAddress(
        mSelector: kAudioObjectPropertyName,
        mScope: kAudioObjectPropertyScopeGlobal,
        mElement: kAudioObjectPropertyElementMain)
    var name: CFString = "" as CFString
    var size = UInt32(MemoryLayout<CFString>.size)
    let status = withUnsafeMutablePointer(to: &name) { ptr in
        AudioObjectGetPropertyData(id, &addr, 0, nil, &size, ptr)
    }
    return status == noErr ? (name as String) : ""
}

func outputStreams(_ id: AudioDeviceID) -> [AudioStreamID] {
    var addr = AudioObjectPropertyAddress(
        mSelector: kAudioDevicePropertyStreams,
        mScope: kAudioDevicePropertyScopeOutput,
        mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(id, &addr, 0, nil, &size) == noErr, size > 0 else {
        return []
    }
    let count = Int(size) / MemoryLayout<AudioStreamID>.size
    var ids = [AudioStreamID](repeating: 0, count: count)
    guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &ids) == noErr else {
        return []
    }
    return ids
}

func findTarget() -> (AudioDeviceID, String)? {
    for id in allDevices() {
        let name = deviceName(id)
        if names.contains(name) && !outputStreams(id).isEmpty {
            return (id, name)
        }
    }
    return nil
}

func configurePhysicalFormat(_ deviceID: AudioDeviceID, rate: Double, bits: UInt32) -> (OSStatus, UInt32) {
    guard let streamID = outputStreams(deviceID).first else {
        return (-1, 0)
    }
    var availAddr = AudioObjectPropertyAddress(
        mSelector: kAudioStreamPropertyAvailablePhysicalFormats,
        mScope: kAudioObjectPropertyScopeGlobal,
        mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    guard AudioObjectGetPropertyDataSize(streamID, &availAddr, 0, nil, &size) == noErr, size > 0 else {
        return (-2, 0)
    }
    let count = Int(size) / MemoryLayout<AudioStreamRangedDescription>.size
    var descs = [AudioStreamRangedDescription](repeating: AudioStreamRangedDescription(), count: count)
    guard AudioObjectGetPropertyData(streamID, &availAddr, 0, nil, &size, &descs) == noErr else {
        return (-3, 0)
    }
    var chosen: AudioStreamBasicDescription? = nil
    for item in descs {
        let lo = item.mSampleRateRange.mMinimum
        let hi = item.mSampleRateRange.mMaximum
        let rateOk = (lo <= rate && rate <= hi) || item.mFormat.mSampleRate == rate
        let bitsOk = bits == 0 || item.mFormat.mBitsPerChannel == bits
        if rateOk && bitsOk {
            var fmt = item.mFormat
            fmt.mSampleRate = rate
            chosen = fmt
            break
        }
    }
    if var fmt = chosen {
        var physAddr = AudioObjectPropertyAddress(
            mSelector: kAudioStreamPropertyPhysicalFormat,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)
        let st = AudioObjectSetPropertyData(streamID, &physAddr, 0, nil,
            UInt32(MemoryLayout<AudioStreamBasicDescription>.size), &fmt)
        return (st, fmt.mBitsPerChannel)
    }
    return (-4, 0)
}

func currentPhysicalBits(_ deviceID: AudioDeviceID) -> UInt32 {
    guard let streamID = outputStreams(deviceID).first else { return 0 }
    var physAddr = AudioObjectPropertyAddress(
        mSelector: kAudioStreamPropertyPhysicalFormat,
        mScope: kAudioObjectPropertyScopeGlobal,
        mElement: kAudioObjectPropertyElementMain)
    var fmt = AudioStreamBasicDescription()
    var size = UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
    if AudioObjectGetPropertyData(streamID, &physAddr, 0, nil, &size, &fmt) == noErr {
        return fmt.mBitsPerChannel
    }
    return 0
}

let waitStart = Date()
var found: (AudioDeviceID, String)? = nil
while Date().timeIntervalSince(waitStart) < connectTimeout {
    if let hit = findTarget() {
        found = hit
        break
    }
    Thread.sleep(forTimeInterval: 0.1)
}
guard let (deviceID, matchedName) = found else {
    fputs("{\"ok\": false, \"error\": \"dac_not_found\", \"searched\": \"\(names.joined(separator: "|"))\"}\n", stderr)
    exit(2)
}

var targetRate = rate
var rateAddr = AudioObjectPropertyAddress(
    mSelector: kAudioDevicePropertyNominalSampleRate,
    mScope: kAudioObjectPropertyScopeGlobal,
    mElement: kAudioObjectPropertyElementMain)
let rateStatus = AudioObjectSetPropertyData(deviceID, &rateAddr, 0, nil, UInt32(MemoryLayout<Float64>.size), &targetRate)
Thread.sleep(forTimeInterval: 0.15)
var formatStatus: OSStatus = 0
var selectedBits: UInt32 = 0
if wantedBits > 0 {
    let res = configurePhysicalFormat(deviceID, rate: rate, bits: wantedBits)
    formatStatus = res.0
    selectedBits = res.1
    Thread.sleep(forTimeInterval: 0.15)
    if currentPhysicalBits(deviceID) != wantedBits {
        let retry = configurePhysicalFormat(deviceID, rate: rate, bits: wantedBits)
        formatStatus = retry.0
        selectedBits = retry.1
        Thread.sleep(forTimeInterval: 0.15)
    }
}
let actualBits = currentPhysicalBits(deviceID)
if selectedBits == 0 {
    selectedBits = actualBits
}

var desc = AudioComponentDescription(
    componentType: kAudioUnitType_Output,
    componentSubType: kAudioUnitSubType_HALOutput,
    componentManufacturer: kAudioUnitManufacturer_Apple,
    componentFlags: 0,
    componentFlagsMask: 0)
guard let comp = AudioComponentFindNext(nil, &desc) else {
    fputs("{\"ok\": false, \"error\": \"no_hal_output\"}\n", stderr)
    exit(3)
}
var unit: AudioUnit?
guard AudioComponentInstanceNew(comp, &unit) == noErr, let audioUnit = unit else {
    fputs("{\"ok\": false, \"error\": \"unit_new_failed\"}\n", stderr)
    exit(3)
}
var dev = deviceID
let devStatus = AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_CurrentDevice,
    kAudioUnitScope_Global, 0, &dev, UInt32(MemoryLayout<AudioDeviceID>.size))
guard devStatus == noErr else {
    fputs("{\"ok\": false, \"error\": \"set_device_failed\", \"status\": \(devStatus)}\n", stderr)
    exit(4)
}

var streamFormat = AudioStreamBasicDescription(
    mSampleRate: rate,
    mFormatID: kAudioFormatLinearPCM,
    mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
    mBytesPerPacket: 8,
    mFramesPerPacket: 1,
    mBytesPerFrame: 8,
    mChannelsPerFrame: 2,
    mBitsPerChannel: 32,
    mReserved: 0)
_ = AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
    kAudioUnitScope_Input, 0, &streamFormat, UInt32(MemoryLayout<AudioStreamBasicDescription>.size))

final class ToneState {
    var phase: Double = 0.0
    let step: Double
    let amp: Float
    var frames: UInt64 = 0
    init(rate: Double, amp: Float) {
        self.step = 2.0 * Double.pi * 440.0 / rate
        self.amp = amp
    }
}
let state = ToneState(rate: rate, amp: amplitude)
var cb = AURenderCallbackStruct(
    inputProc: { inRefCon, _, _, _, inNumberFrames, ioData in
        let s = Unmanaged<ToneState>.fromOpaque(inRefCon).takeUnretainedValue()
        guard let ioData = ioData else { return noErr }
        let buffers = UnsafeMutableAudioBufferListPointer(ioData)
        if let buf = buffers.first, let ptr = buf.mData?.assumingMemoryBound(to: Float.self) {
            let count = Int(inNumberFrames)
            for i in 0..<count {
                let sample = s.amp == 0 ? Float(0) : Float(sin(s.phase)) * s.amp
                s.phase += s.step
                if s.phase >= 2.0 * Double.pi { s.phase -= 2.0 * Double.pi }
                ptr[i * 2] = sample
                ptr[i * 2 + 1] = sample
            }
            s.frames += UInt64(count)
        }
        return noErr
    },
    inputProcRefCon: Unmanaged.passUnretained(state).toOpaque()
)
_ = AudioUnitSetProperty(audioUnit, kAudioUnitProperty_SetRenderCallback,
    kAudioUnitScope_Input, 0, &cb, UInt32(MemoryLayout<AURenderCallbackStruct>.size))
guard AudioUnitInitialize(audioUnit) == noErr, AudioOutputUnitStart(audioUnit) == noErr else {
    fputs("{\"ok\": false, \"error\": \"unit_start_failed\"}\n", stderr)
    exit(5)
}

let streamStart = Date()
while Date().timeIntervalSince(streamStart) < streamTimeout {
    Thread.sleep(forTimeInterval: 0.1)
    if !allDevices().contains(deviceID) {
        break
    }
}
AudioOutputUnitStop(audioUnit)
AudioUnitUninitialize(audioUnit)
AudioComponentInstanceDispose(audioUnit)
let elapsed = Date().timeIntervalSince(streamStart)
print("{\"ok\": true, \"device\": \"\(matchedName)\", \"rate\": \(Int(rate)), \"bits\": \(actualBits), \"format_set_status\": \(formatStatus), \"rate_set_status\": \(rateStatus), \"frames\": \(state.frames), \"seconds\": \(String(format: "%.2f", elapsed))}")
"""


SOAK_SCRIPT = """\
# Use English and pause a wired track with 70s left, at low volume.
# Leaves Gain Low and DAC filter Fast, low latency.
open home
set Gain=Low
set DAC filter=Fast, low latency
open nowplaying
wait 1000
play
wait 2000
set Gain=High
set DAC filter=Slow, low latency
soak 1
set Gain=Low
set DAC filter=Fast, low latency
wait 2000
play
wait 1000
open home
reconnect
"""

CHECKLIST = """\
On-player pre-release checklist

- Power cycle the player and confirm it starts reliably.
- Play a track on the 3.5mm and 4.4mm outputs and listen for dropouts.
- Confirm settings and playback state are kept across restarts.
- Verify charging with a USB power source.
- Copy files to and from the player over normal USB file transfer. Eject and verify playback of a copied track.
- In USB DAC mode, open the menu, switch gain, and change the host sample rate during playback. Listen after each change.
"""


def find_elf_for_address(address, build=None, elf_dir=None):
    name = "echo_usb_overlay.elf" if address in OVERLAY_RANGE else (
        "echo_device.elf" if address in RESIDENT_RANGE else None)
    if name is None:
        return None
    candidates = []
    if elf_dir is not None:
        candidates.append(Path(elf_dir) / name)
    if build:
        clean = build.removesuffix("+").lower()
        reports = SCRATCH_ROOT / "reports"
        if reports.is_dir():
            for match in sorted(reports.glob(f"candidate-{clean}*"), reverse=True):
                candidates.append(match / name)
    repo_root = Path(__file__).resolve().parents[1]
    candidates.append(repo_root / "firmware/build/device/platform/device" / name)
    for path in candidates:
        if path.is_file():
            return path
    return None


def symbolize_pc(address, build=None, elf_dir=None):
    if not isinstance(address, int) or address == 0:
        return None
    elf = find_elf_for_address(address, build=build, elf_dir=elf_dir)
    if elf is None:
        return None
    tool = shutil.which("arm-none-eabi-addr2line") or (
        str(ARM_ADDR2LINE_FALLBACK) if ARM_ADDR2LINE_FALLBACK.is_file() else None)
    if tool is None:
        return None
    # Thumb-2 return addresses have bit 0 set; step back 2 bytes to the call site.
    call_pc = (address & ~1) - 2 if (address & 1) else address
    result = subprocess.run([tool, "-f", "-p", "-e", str(elf), f"0x{call_pc:08x}"],
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        return None
    line = result.stdout.strip().splitlines()[0] if result.stdout.strip() else ""
    if not line or line.startswith("??"):
        return None
    return line


def check_report(tool, build, max_underruns, require_dac=False, max_dac_gap_us=None,
                 expected_rate=None, expected_bits=None, max_dropped_events=0, elf_dir=None):
    checks = []

    def check(name, value, expected, ok):
        checks.append({"name": name, "value": value, "expected": expected, "ok": ok})

    try:
        reports, unreadable = tool.read_reports()
        if not reports and not unreadable:
            unreadable = [{"error": "no_report", "message": "the debug folder holds no report"}]
    except (echo_debug.DebugError, OSError, ValueError) as error:
        code = getattr(error, "code", "io_error" if isinstance(error, OSError) else "bad_report")
        reports, unreadable = [], [{"error": code, "message": str(error)}]

    check("report.readable", unreadable, "at least one report and no unreadable slots", not unreadable)
    path = None
    values = {}
    if reports:
        newest = reports[0]
        path = newest["path"]
        report = newest["report"]
        values = report["values"]
        check("report.truncated", report["truncated"], False, report["truncated"] is False)
        cold_start = values.get("report.cold_start")
        check("report.cold_start", cold_start, 0, cold_start == 0)

        tag = values.get("firmware.build")
        check("firmware.build_clean", tag, "no trailing +", tag is None or not tag.endswith("+"))
        if build is not None:
            clean_tag = tag.removesuffix("+").lower() if tag is not None else None
            check("firmware.build", tag, build, clean_tag == build)

        warm = values.get("start.warm")
        for name in ("fault.cfsr", "fault.hfsr", "fault.pc"):
            value = values.get(name)
            cold_unknown = warm == 0 and value is None
            check(name, value, "0 (or unknown on cold start)" if warm == 0 else 0,
                  value == 0 or cold_unknown)

        underruns = values.get("audio.output_underruns")
        check("audio.output_underruns", underruns, f"0..{max_underruns}",
              isinstance(underruns, int) and 0 <= underruns <= max_underruns)

        for name, allowed in (("usb.writes_after_eject", 0), ("start.last_step_finished", 1)):
            value = values.get(name)
            check(name, value, [None, allowed], value in (None, allowed))

        millivolts = values.get("battery.millivolts")
        usb_power = (values.get("power.usb_powered") == 1 or values.get("power.usb_power") == 1
                     or values.get("power.vbus") == 1 or values.get("battery.usb_power") == 1)
        max_mv = 4900 if usb_power else 4500
        check("battery.millivolts", millivolts, f"unknown or 3000..{max_mv} mV",
              millivolts is None or isinstance(millivolts, int) and 3000 <= millivolts <= max_mv)
        dropped = values.get("events.dropped", 0)
        if max_dropped_events is not None:
            expected_dropped = "absent or 0" if max_dropped_events == 0 else f"<= {max_dropped_events}"
            check("events.dropped", values.get("events.dropped"), expected_dropped,
                  isinstance(dropped, int) and 0 <= dropped <= max_dropped_events)

        if require_dac or "usb.dac.packets" in values:
            rate = values.get("usb.dac.rate")
            if expected_rate is not None:
                check("usb.dac.rate", rate, expected_rate, rate == expected_rate)
            bits = values.get("usb.dac.bits")
            if expected_bits is not None and expected_bits > 0:
                check("usb.dac.bits", bits, expected_bits, bits == expected_bits)
            packets = values.get("usb.dac.packets")
            check("usb.dac.packets", packets, "> 0", isinstance(packets, int) and packets > 0)
            dac_underruns = values.get("usb.dac.underruns")
            check("usb.dac.underruns", dac_underruns, f"0..{max_underruns}",
                  isinstance(dac_underruns, int) and 0 <= dac_underruns <= max_underruns)
            for name in ("usb.dac.overflows", "usb.dac.realigned"):
                value = values.get(name)
                check(name, value, 0, value == 0)
            gap = values.get("usb.dac.max_gap_us")
            if max_dac_gap_us is not None:
                check("usb.dac.max_gap_us", gap, f"0..{max_dac_gap_us}",
                      isinstance(gap, int) and 0 <= gap <= max_dac_gap_us)
            elif gap is not None:
                check("usb.dac.max_gap_us", gap, "recorded", isinstance(gap, int) and gap >= 0)
            gap_pc = values.get("usb.dac.max_gap_pc")
            if isinstance(gap_pc, int) and gap_pc != 0:
                symbol = symbolize_pc(gap_pc, build=tag, elf_dir=elf_dir)
                rendered = f"0x{gap_pc:08x} ({symbol})" if symbol else f"0x{gap_pc:08x}"
                check("usb.dac.max_gap_pc", rendered, "recorded", True)

    recent = [{key: value for key, value in item.items() if key != "report"} for item in reports]
    return {"ok": all(item["ok"] for item in checks), "path": path, "checks": checks,
            "values": values, "reports": recent}


def print_scorecard(result):
    outcome = "PASS" if result["ok"] else "FAIL"
    report = result["path"] or "no readable report"
    print(f"{outcome} {report}")
    for check in result["checks"]:
        outcome = "PASS" if check["ok"] else "FAIL"
        value = json.dumps(check["value"])
        expected = f" (expected {check['expected']})" if not check["ok"] else ""
        print(f"  {outcome} {check['name']} = {value}{expected}")
        if check["name"] == "report.cold_start" and check["value"] == 1:
            print(f"  {echo_debug.COLD_START_ADVICE}")
    older = result["reports"][1:]
    if older:
        entries = ", ".join(f"{item['file']} (sequence {item['sequence']})" for item in older)
        print(f"  Earlier reports: {entries}")


def sha7(value):
    if not re.fullmatch(r"[0-9a-fA-F]{7}", value):
        raise argparse.ArgumentTypeError("expected seven hexadecimal digits")
    return value.lower()


def nonnegative_int(value):
    count = int(value)
    if count < 0:
        raise argparse.ArgumentTypeError("expected a nonnegative integer")
    return count


def parse_rates(value):
    rates = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        rate = int(part)
        if rate not in VALID_RATES:
            raise argparse.ArgumentTypeError(f"unsupported rate {rate}; choose from {VALID_RATES}")
        rates.append(rate)
    if not rates:
        raise argparse.ArgumentTypeError("at least one sample rate is required")
    return rates


def parse_bits_list(value):
    bits_list = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        bits = int(part)
        if bits not in VALID_BITS or bits == 0:
            raise argparse.ArgumentTypeError(f"unsupported bit depth {bits}; choose from (16, 24, 32)")
        bits_list.append(bits)
    if not bits_list:
        raise argparse.ArgumentTypeError("at least one bit depth is required")
    return bits_list


def stream_to_dac(rate, amplitude, connect_timeout, stream_timeout, bits=0):
    names = "|".join([echo_debug.IDENTITY.get("usb_dac_name", echo_debug.IDENTITY["name"] + " DAC"),
                      echo_debug.IDENTITY["name"]])
    result = subprocess.run(
        ["swift", "-", names, str(rate), str(bits), str(amplitude),
         str(connect_timeout), str(stream_timeout)],
        input=COREAUDIO_STREAM_SWIFT,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        return {"ok": False, "returncode": result.returncode, "detail": detail}
    for line in reversed(result.stdout.splitlines()):
        line = line.strip()
        if line.startswith("{"):
            return json.loads(line)
    return {"ok": False, "returncode": result.returncode, "detail": result.stdout.strip()}


def wait_for_settled_luns(tool, expect_card=False, settle_seconds=6.0):
    """Wait until any attached SD LUN finishes mounting on macOS before ejecting."""
    found = tool.locate()
    usb = found.get("usb")
    if not usb or usb.get("card_volumes"):
        return found
    has_card_hint = expect_card or bool(usb.get("has_card_medium"))
    if not has_card_hint and not isinstance(getattr(tool, "host", None), echo_debug.MacHost):
        return found
    wait_limit = settle_seconds if has_card_hint else 2.5
    deadline = tool.clock() + wait_limit
    while tool.clock() < deadline:
        tool.sleep(0.25)
        found = tool.locate()
        usb = found.get("usb")
        if usb and usb.get("card_volumes"):
            return found
        if usb and usb.get("has_card_medium") and wait_limit < settle_seconds:
            deadline = tool.clock() + (settle_seconds - wait_limit)
            wait_limit = settle_seconds
    return found


def current_dropped_events(tool):
    try:
        _, report = tool.read_report()
        dropped = report["values"].get("events.dropped", 0)
        return dropped if isinstance(dropped, int) else 0
    except (echo_debug.DebugError, OSError, ValueError):
        return 0


def run_dac_test(tool, build, rate, silent, max_underruns, max_dac_gap_us, timeout,
                 bits=0, streamer=stream_to_dac, expect_card=False, allow_prior_drops=False,
                 elf_dir=None):
    found = wait_for_settled_luns(tool, expect_card=expect_card)
    had_card = bool(found.get("usb") and found["usb"].get("card_volumes"))
    root = Path(found["volume"]) if found.get("volume") else tool.root()
    target_build = build or tool.read_report()[1]["values"].get("firmware.build") or "any"
    prior_drops = current_dropped_events(tool) if allow_prior_drops else 0
    data = echo_debug.prepare_script(DAC_TEST_SCRIPT, target_build)
    tool.write(root, root / echo_debug.DATA_DIRECTORY / echo_debug.DEBUG_DIRECTORY / echo_debug.RUN_FILE, data)
    ejected = tool.eject(wait_for_settled_luns(tool, expect_card=had_card or expect_card))
    amplitude = 0.0 if silent else 0.008
    if bits:
        stream = streamer(rate, amplitude, min(timeout, 20), min(timeout, 25), bits=bits)
    else:
        stream = streamer(rate, amplitude, min(timeout, 20), min(timeout, 25))
    tool.wait_for_return(timeout)
    wait_for_settled_luns(tool, expect_card=had_card or expect_card)
    scorecard = check_report(
        tool, build, max_underruns, require_dac=True, max_dac_gap_us=max_dac_gap_us,
        expected_rate=rate, expected_bits=bits if bits else None,
        max_dropped_events=None if allow_prior_drops else 0,
        elf_dir=elf_dir,
    )
    ok = bool(stream.get("ok")) and scorecard["ok"]
    return {"ok": ok, "rate": rate, "bits": bits, "had_card": had_card, "prior_drops": prior_drops,
            "ejected": ejected, "stream": stream, "scorecard": scorecard}


def _nocache_write_read(path, payload):
    expected = hashlib.sha256(payload).hexdigest()
    started = time.monotonic()
    with path.open("wb") as handle:
        if sys.platform == "darwin":
            try:
                fcntl.fcntl(handle.fileno(), 48, 1)
            except OSError:
                pass
        handle.write(payload)
        handle.flush()
        os.fsync(handle.fileno())
    write_s = max(time.monotonic() - started, 1e-6)
    read_start = time.monotonic()
    with path.open("rb") as handle:
        if sys.platform == "darwin":
            try:
                fcntl.fcntl(handle.fileno(), 48, 1)
            except OSError:
                pass
        actual = hashlib.sha256(handle.read()).hexdigest()
    read_s = max(time.monotonic() - read_start, 1e-6)
    path.unlink()
    sidecar = path.with_name(f"._{path.name}")
    if sidecar.exists():
        sidecar.unlink()
    mb = len(payload) / (1024 * 1024)
    return {
        "ok": actual == expected,
        "bytes": len(payload),
        "sha256": actual,
        "write_mb_s": round(mb / write_s, 2),
        "read_mb_s": round(mb / read_s, 2),
    }


def run_storage_transfer_check(tool, size_bytes=STORAGE_TEST_BYTES):
    root = tool.root()
    target = root / echo_debug.DATA_DIRECTORY / echo_debug.DEBUG_DIRECTORY / STORAGE_TEST_FILE
    return _nocache_write_read(target, os.urandom(size_bytes))


def script_line_failures(report):
    lines = report.get("script", {}).get("lines", [])
    return [line for line in lines if line.get("status") != "ok"]


def profile_top_functions(tool, build=None, elf_dir=None, top=6):
    elf = find_elf_for_address(0x03050000, build=build, elf_dir=elf_dir)
    if elf is None:
        return []
    try:
        prof = tool.profile(elf, top=top)
        return prof["profile"]["functions"][:top]
    except (echo_debug.DebugError, OSError, KeyError, ValueError):
        return []


def run_script_playbook(tool, script_text, build, max_underruns, timeout,
                        expect_card=False, elf_dir=None):
    found = wait_for_settled_luns(tool, expect_card=expect_card)
    had_card = bool(found.get("usb") and found["usb"].get("card_volumes"))
    root = Path(found["volume"]) if found.get("volume") else tool.root()
    target_build = build or tool.read_report()[1]["values"].get("firmware.build") or "any"
    data = echo_debug.prepare_script(script_text, target_build)
    tool.write(root, root / echo_debug.DATA_DIRECTORY / echo_debug.DEBUG_DIRECTORY / echo_debug.RUN_FILE, data)
    tool.eject(wait_for_settled_luns(tool, expect_card=had_card or expect_card))
    tool.wait_for_return(timeout)
    wait_for_settled_luns(tool, expect_card=had_card or expect_card)
    _, report = tool.read_report()
    scorecard = check_report(tool, build, max_underruns, max_dropped_events=None, elf_dir=elf_dir)
    bad_lines = script_line_failures(report)
    script_state = scorecard["values"].get("script.state")
    ok = scorecard["ok"] and script_state == "finished" and not bad_lines
    return {
        "ok": ok,
        "had_card": had_card,
        "script_state": script_state,
        "bad_lines": bad_lines,
        "measures": report.get("script", {}).get("measures", []),
        "rtc": report.get("script", {}).get("rtc", []),
        "screenshots": report.get("script", {}).get("screenshots", []),
        "events": report.get("events", []),
        "profile_top": profile_top_functions(tool, build=build, elf_dir=elf_dir),
        "scorecard": scorecard,
    }


def run_storage_playbook(tool, size_bytes=STORAGE_PLAYBOOK_BYTES, burst_bytes=STORAGE_BURST_BYTES,
                         burst_count=STORAGE_BURST_COUNT):
    found = wait_for_settled_luns(tool)
    root = Path(found["volume"]) if found.get("volume") else tool.root()
    debug_dir = root / echo_debug.DATA_DIRECTORY / echo_debug.DEBUG_DIRECTORY
    seq = _nocache_write_read(debug_dir / STORAGE_TEST_FILE, os.urandom(size_bytes))
    bursts = [_nocache_write_read(debug_dir / f"burst{i}.tmp", os.urandom(burst_bytes))
              for i in range(burst_count)]
    burst_ok = all(item["ok"] for item in bursts)
    burst_write = round(sum(item["write_mb_s"] for item in bursts) / len(bursts), 2) if bursts else 0.0
    burst_read = round(sum(item["read_mb_s"] for item in bursts) / len(bursts), 2) if bursts else 0.0
    usb_timing = None
    try:
        _, report = tool.read_report()
        usb_timing = report["values"].get("usb.timing")
    except (echo_debug.DebugError, OSError):
        pass
    return {
        "ok": seq["ok"] and burst_ok,
        "had_card": bool(found.get("usb") and found["usb"].get("card_volumes")),
        "sequential": seq,
        "burst": {"ok": burst_ok, "count": burst_count, "bytes_each": burst_bytes,
                  "write_mb_s": burst_write, "read_mb_s": burst_read},
        "usb_timing": usb_timing,
    }


def extract_scan_timings(events):
    scans = [event for event in events if event.get("kind") == "scan"]
    started = next((e["time_ms"] for e in scans if e.get("detail", "").startswith("start")), None)
    done = next((e["time_ms"] for e in reversed(scans)
                 if any(tok in e.get("detail", "") for tok in ("up_to_date", "covers_done", "library"))), None)
    duration = (done - started) if (started is not None and done is not None and done >= started) else (
        0 if any("up_to_date" in e.get("detail", "") for e in scans) else None
    )
    return {"events": scans, "duration_ms": duration}


def _folder_has_audio(path):
    try:
        for child in path.iterdir():
            if child.name.startswith("."):
                continue
            if child.is_file() and child.suffix.lower() in AUDIO_EXTENSIONS:
                return True
            if child.is_dir() and _folder_has_audio(child):
                return True
    except OSError:
        return False
    return False


def discover_folder_catalog(found):
    internal_root = Path(found["volume"]) if found.get("volume") else None
    internal_has_music = False
    if internal_root is not None and internal_root.is_dir():
        for child in internal_root.iterdir():
            if child.name.startswith("."):
                continue
            if child.is_dir() and _folder_has_audio(child):
                internal_has_music = True
                break
            if child.is_file() and child.suffix.lower() in AUDIO_EXTENSIONS:
                internal_has_music = True
                break
    usb = found.get("usb") or {}
    card_volumes = usb.get("card_volumes") or []
    if not card_volumes:
        return {"internal_has_music": internal_has_music, "card_root": None, "folders": {}}
    card_root = Path(card_volumes[0]["mount_point"])
    dirs = sorted(
        (p for p in card_root.iterdir()
         if p.is_dir() and not p.name.startswith(".") and p.name != "System Volume Information"),
        key=lambda p: p.name.casefold(),
    )
    offset = 1 if internal_has_music else 0
    db_path = card_root / echo_debug.DATA_DIRECTORY / "library.db"
    db_bytes = None
    try:
        if db_path.is_file():
            db_bytes = db_path.read_bytes()
    except OSError:
        db_bytes = None
    folders = {}
    for row_idx, folder in enumerate(dirs, start=offset):
        raw_tracks = sorted(
            (p.name for p in folder.iterdir()
             if p.is_file() and not p.name.startswith(".") and p.suffix.lower() in AUDIO_EXTENSIONS),
            key=str.casefold,
        )
        if db_bytes is not None:
            tracks = [
                name for name in raw_tracks
                if f"{folder.name}/{name}".encode("utf-8") in db_bytes
            ]
        else:
            tracks = raw_tracks
        folders[folder.name] = {"row": row_idx, "tracks": tracks}
    return {"internal_has_music": internal_has_music, "card_root": str(card_root), "folders": folders}


def folder_enter_lines(folder_row):
    down_folder = ["down"] * folder_row + ["play"]
    return [
        "open coverflow",
        "wait 200",
        "back,down,down,down,down,down,play",
        "wait 350",
        ",".join(down_folder),
        "wait 300",
    ]


def build_codec_batch_script(folder_row, items):
    """Build a script playing up to 4 tracks/windows inside one folder."""
    lines = folder_enter_lines(folder_row)
    prev_idx = None
    for track_idx, _label, wait_ms, measure_ms, screen_off, _expect_audio in items:
        if prev_idx is None:
            lines.append(",".join(["down"] * track_idx + ["play"]))
        elif track_idx != prev_idx:
            step = "down" if track_idx > prev_idx else "up"
            lines.append(",".join(["back"] + [step] * abs(track_idx - prev_idx) + ["play"]))
        if screen_off:
            lines.extend(["screen off", "wait 400", f"measure {measure_ms}", "screen on", "wait 200"])
        else:
            lines.extend([f"wait {wait_ms}", f"measure {measure_ms}"])
        prev_idx = track_idx
    if items and items[-1][5]:
        lines.append("play")
    lines.extend(["open home", "reconnect"])
    return "\n".join(lines) + "\n"


def _find_track_index(tracks, prefix):
    for idx, name in enumerate(tracks):
        if name.lower().startswith(prefix.lower()):
            return idx, name
    return None, None


def build_codec_plan(catalog):
    """Return a list of batch dicts: {name, folder, script, windows: [{label, expect_audio}]}."""
    folders = catalog.get("folders", {})
    batches = []

    def add_batch(name, folder_name, specs):
        info = folders.get(folder_name)
        if not info:
            return
        tracks = info["tracks"]
        items = []
        windows = []
        for prefix, label, wait_ms, measure_ms, screen_off, expect_audio, buf_range in specs:
            idx, real_name = _find_track_index(tracks, prefix)
            if idx is None:
                continue
            items.append((idx, label, wait_ms, measure_ms, screen_off, expect_audio))
            windows.append({
                "label": label,
                "track": real_name,
                "expect_audio": expect_audio,
                "buf_range": buf_range,
            })
        if items:
            batches.append({
                "name": name,
                "folder": folder_name,
                "script": build_codec_batch_script(info["row"], items),
                "windows": windows,
            })

    add_batch("batch4_hires", "Batch 4 Checks", [
        ("01 FLAC 96k", "FLAC 96k/24 small blocks (scr ON)", 800, 2400, False, True, (100, 175)),
        ("01 FLAC 96k", "FLAC 96k/24 small blocks (scr OFF)", 400, 2400, True, True, (90, 175)),
        ("02 FLAC 44.1k", "FLAC 44.1k (pre-switch)", 700, 2000, False, True, (280, 345)),
        ("03 FLAC 48k", "FLAC 48k (rate switch)", 700, 2000, False, True, (255, 315)),
    ])
    add_batch("batch4_lossy", "Batch 4 Checks", [
        ("05 FLAC 1 min", "FLAC 1m (44.1k)", 700, 1800, False, True, (280, 345)),
        ("07 FLAC 20 s", "FLAC 20s (44.1k)", 700, 1800, False, True, (280, 345)),
        ("08 Opus 48k", "Opus 48k 20ms", 700, 2200, False, True, (255, 315)),
        ("09 Vorbis 44.1k", "Vorbis 44.1k", 700, 2200, False, True, (280, 345)),
    ])
    add_batch("codecs_1", "Codec Checks", [
        ("01 WAV 44k", "WAV 44.1k/16 stereo", 700, 2000, False, True, (280, 345)),
        ("02 WAV 96k", "WAV 96k/24 stereo", 700, 2000, False, True, (100, 175)),
        ("03 WAV 8k", "WAV 8k/16 mono", 700, 2000, False, True, (390, 465)),
        ("04 FLAC 44k", "FLAC 44.1k/24 stereo", 700, 2000, False, True, (280, 345)),
    ])
    add_batch("codecs_2", "Codec Checks", [
        ("05 FLAC 192k", "FLAC 192k/24 stereo", 800, 2400, False, True, (40, 95)),
        ("06 MP3 CBR", "MP3 CBR 320k", 700, 2000, False, True, (280, 345)),
        ("07 MP3 VBR", "MP3 VBR 48k", 700, 2000, False, True, (255, 315)),
        ("08 AAC-LC 44k", "AAC-LC 44.1k stereo", 700, 2000, False, True, (280, 345)),
    ])
    add_batch("codecs_3", "Codec Checks", [
        ("09 AAC-LC 16k", "AAC-LC 16k mono", 700, 2000, False, True, (390, 465)),
        ("10 ALAC 44k", "ALAC 44.1k/24 stereo", 700, 2000, False, True, (280, 345)),
        ("11 Vorbis 44k", "Vorbis 44.1k stereo", 700, 2000, False, True, (280, 345)),
        ("12 Vorbis 48k", "Vorbis 48k stereo", 700, 2000, False, True, (255, 315)),
    ])
    add_batch("codecs_4", "Codec Checks", [
        ("13 Opus 48k", "Opus 48k stereo", 700, 2000, False, True, (255, 315)),
        ("14 Ogg Opus", "Ogg Opus 48k stereo", 700, 2000, False, True, (255, 315)),
    ])
    add_batch("beta_codecs", "Beta Checks", [
        ("15 AIFF", "AIFF 44.1k/16 stereo", 700, 2000, False, True, (280, 345)),
        ("16 WMA", "WMA v2 128k stereo", 700, 2000, False, True, (280, 345)),
        ("17 APE", "APE 16-bit stereo", 700, 1800, False, True, (255, 465)),
        ("19 DSD64", "DSD64 DSF 2.8MHz", 500, 1800, False, True, (115, 195)),
    ])
    return batches


def _measure_audio_active(measure):
    val = measure.get("audio")
    if val is None:
        val = measure.get("audio_running")
    return val == 1


def _measure_core1_percent(measure):
    permille = measure.get("core1_busy_permille")
    if isinstance(permille, int):
        return round(permille / 10.0, 1)
    pct = measure.get("core1_busy_percent")
    return float(pct) if isinstance(pct, (int, float)) else None


def run_codecs_playbook(tool, build, max_underruns, timeout, expect_card=False, elf_dir=None):
    found = wait_for_settled_luns(tool, expect_card=True)
    had_card = bool(found.get("usb") and found["usb"].get("card_volumes"))
    catalog = discover_folder_catalog(found)
    plan = build_codec_plan(catalog)
    if not plan:
        return {
            "ok": False,
            "had_card": had_card,
            "error": "no_codec_test_folders_found",
            "batches": [],
            "tracks": [],
        }
    batch_results = []
    track_rows = []
    all_ok = True
    for batch in plan:
        res = run_script_playbook(tool, batch["script"], build, max_underruns, timeout,
                                  expect_card=had_card or expect_card, elf_dir=elf_dir)
        had_card = had_card or res["had_card"]
        measures = res.get("measures", [])
        batch_ok = res["ok"] and len(measures) == len(batch["windows"])
        for win, m in zip(batch["windows"], measures):
            und = m.get("underruns") or 0
            out_und = m.get("output_underruns") or 0
            active = _measure_audio_active(m)
            blocks = m.get("blocks") or 0
            buf_ms = m.get("buffered_ms")
            buf_range = win.get("buf_range")
            buf_ok = True
            if win["expect_audio"] and buf_range is not None:
                buf_ok = isinstance(buf_ms, int) and buf_range[0] <= buf_ms <= buf_range[1]
            row_ok = (
                res["ok"]
                and und <= max_underruns
                and out_und <= max_underruns
                and (not win["expect_audio"] or (active and blocks > 0 and buf_ok))
            )
            batch_ok = batch_ok and row_ok
            track_rows.append({
                "batch": batch["name"],
                "label": win["label"],
                "track": win["track"],
                "expect_audio": win["expect_audio"],
                "audio": 1 if active else 0,
                "blocks": blocks,
                "buffered_ms": m.get("buffered_ms"),
                "core0_pct": m.get("core0_busy_percent"),
                "core1_pct": _measure_core1_percent(m),
                "audio_busy_pct": m.get("audio_busy_percent"),
                "read_max_us": m.get("read_max_us"),
                "handoff_max_us": m.get("handoff_max_us"),
                "underruns": und,
                "output_underruns": out_und,
                "ok": row_ok,
            })
        all_ok = all_ok and batch_ok
        batch_results.append({
            "name": batch["name"],
            "folder": batch["folder"],
            "ok": batch_ok,
            "script_state": res["script_state"],
            "bad_lines": res["bad_lines"],
            "profile_top": res.get("profile_top", []),
        })
    return {
        "ok": all_ok,
        "had_card": had_card,
        "batches": batch_results,
        "tracks": track_rows,
    }


def decode_owner_settings(root):
    """Read the owner's saved settings.bin into set-command labels so sweeps restore them."""
    path = Path(root) / echo_debug.DATA_DIRECTORY / "settings.bin"
    defaults = {
        "gain": "Low",
        "dac_filter": "Fast, low latency",
        "equalizer": "Off",
        "crossfeed": "Off",
        "replay_gain": "Off",
        "prevent_clipping": "On",
        "skip_silence": "Off",
        "speech_boost": "Off",
        "playback_speed": "Normal (1×)",
        "now_playing_style": "Cover",
        "orientation": "Landscape",
        "text_size": "Medium",
    }
    try:
        data = path.read_bytes()
    except OSError:
        return defaults
    if len(data) < 58 or data[:4] != b"DSST":
        return defaults
    ext = data[38:54]

    def pick(key, index):
        opts = SETTINGS_LABELS[key]
        return opts[index] if 0 <= index < len(opts) else defaults[key]

    speed_offset = ext[3] - 256 if ext[3] >= 128 else ext[3]
    style_idx = ((ext[13] & 0x20) >> 5) | ((ext[14] & 0x80) >> 6)
    return {
        "gain": pick("gain", data[8]),
        "dac_filter": pick("dac_filter", ext[14] & 7),
        "equalizer": pick("equalizer", data[18]),
        "crossfeed": pick("crossfeed", (ext[14] >> 5) & 3),
        "replay_gain": pick("replay_gain", data[5] & 3),
        "prevent_clipping": pick("prevent_clipping", 1 if (data[5] & 4) else 0),
        "skip_silence": pick("skip_silence", 1 if (ext[13] & 0x40) else 0),
        "speech_boost": pick("speech_boost", 1 if (ext[13] & 0x80) else 0),
        "playback_speed": pick("playback_speed", 2 + speed_offset),
        "now_playing_style": pick("now_playing_style", style_idx),
        "orientation": pick("orientation", data[26]),
        "text_size": pick("text_size", data[24]),
    }


def build_settings_restore_script(owner_settings):
    lines = [
        f"set {SETTINGS_DISPLAY_NAMES[key]}={owner_settings[key]}"
        for key in SETTINGS_DISPLAY_NAMES
        if key in owner_settings
    ]
    lines.extend(["open home", "reconnect"])
    return "\n".join(lines) + "\n"


def build_settings_plan(folder_row, track_idx=0):
    """Build 4 settings/DSP sweep batches during active 96k/24 FLAC playback."""
    enter = folder_enter_lines(folder_row)
    play_first = ",".join(["down"] * track_idx + ["play"])
    batch1 = "\n".join(enter + [
        play_first,
        "set Gain=Low",
        "set DAC filter=Fast, low latency",
        "set Equalizer=Off",
        "set Crossfeed=Off",
        "set ReplayGain=Off",
        "set Prevent clipping=Off",
        "set Skip silence=Off",
        "set Speech boost=Off",
        "set Now Playing style=Cover",
        "set Orientation=Landscape",
        "wait 800",
        "measure 2200",
        "set Equalizer=Bass",
        "wait 400",
        "measure 2200",
        "set Equalizer=Off",
        "set Crossfeed=High",
        "wait 400",
        "measure 2200",
        "set Equalizer=Bass",
        "set ReplayGain=Track",
        "set Prevent clipping=On",
        "set Skip silence=On",
        "set Speech boost=On",
        "wait 400",
        "measure 2200",
        "play",
        "reconnect",
    ]) + "\n"

    batch2 = "\n".join([
        "open nowplaying",
        "wait 300",
        "play",
        "set Equalizer=Off",
        "set Crossfeed=Off",
        "set ReplayGain=Off",
        "set Skip silence=Off",
        "set Speech boost=Off",
        "set Gain=Low",
        "set DAC filter=Fast, low latency",
        "wait 700",
        "measure 2200",
        "set Gain=High",
        "set DAC filter=Slow, low latency",
        "wait 400",
        "measure 2200",
        "set DAC filter=Non-oversampling",
        "wait 400",
        "measure 2200",
        "set Playback speed=1.5×",
        "wait 400",
        "measure 2200",
        "set Playback speed=Normal (1×)",
        "set Gain=Low",
        "set DAC filter=Fast, low latency",
        "play",
        "reconnect",
    ]) + "\n"

    batch3 = "\n".join([
        "open nowplaying",
        "wait 300",
        "play",
        "set Now Playing style=Cover",
        "wait 700",
        "measure 2200",
        "set Now Playing style=Cassette",
        "wait 400",
        "measure 2200",
        "set Now Playing style=VU meters",
        "wait 400",
        "measure 2200",
        "set Now Playing style=Spectrum",
        "wait 400",
        "measure 2200",
        "play",
        "reconnect",
    ]) + "\n"

    batch4 = "\n".join([
        "open nowplaying",
        "wait 300",
        "play",
        "set Now Playing style=Cover",
        "set Orientation=Portrait",
        "set Text size=Small",
        "wait 700",
        "measure 2200",
        "set Equalizer=Bass",
        "set Crossfeed=High",
        "set ReplayGain=Track",
        "set Now Playing style=Spectrum",
        "set Text size=Large",
        "wait 500",
        "measure 2200",
        "screen off",
        "wait 400",
        "measure 2200",
        "screen on",
        "set Orientation=Landscape",
        "set Equalizer=Off",
        "set Crossfeed=Off",
        "set ReplayGain=Off",
        "set Now Playing style=Cover",
        "wait 500",
        "measure 2200",
        "play",
        "reconnect",
    ]) + "\n"

    return [
        {
            "name": "dsp_core",
            "script": batch1,
            "labels": [
                "Baseline (DSP Off, Cover)",
                "Equalizer=Bass",
                "Crossfeed=High",
                "All DSP (EQ+Xfeed+RG+Clip+Skip+Speech)",
            ],
        },
        {
            "name": "dac_and_speed",
            "script": batch2,
            "labels": [
                "Gain=Low + Fast low-lat",
                "Gain=High + Slow low-lat",
                "DAC filter=Non-oversampling",
                "Playback speed=1.5×",
            ],
        },
        {
            "name": "now_playing_styles",
            "script": batch3,
            "labels": [
                "Style=Cover",
                "Style=Cassette",
                "Style=VU meters",
                "Style=Spectrum",
            ],
        },
        {
            "name": "layout_and_stress",
            "script": batch4,
            "labels": [
                "Portrait + Text Small",
                "Worst-Case (All DSP+Spectrum+Portrait+Large)",
                "Screen Off (All DSP Active)",
                "Screen On (Landscape + DSP Off)",
            ],
        },
    ]


def run_settings_playbook(tool, build, max_underruns, timeout, expect_card=False, elf_dir=None):
    found = wait_for_settled_luns(tool, expect_card=True)
    had_card = bool(found.get("usb") and found["usb"].get("card_volumes"))
    root = Path(found["volume"]) if found.get("volume") else tool.root()
    owner_settings = decode_owner_settings(root)
    catalog = discover_folder_catalog(found)
    batch4 = catalog.get("folders", {}).get("Batch 4 Checks")
    if not batch4:
        return {
            "ok": False,
            "had_card": had_card,
            "error": "batch4_folder_not_found",
            "owner_settings": owner_settings,
            "rows": [],
            "restored": False,
        }
    track_idx, track_name = _find_track_index(batch4["tracks"], "01 FLAC 96k")
    if track_idx is None:
        track_idx, track_name = 0, (batch4["tracks"][0] if batch4["tracks"] else "track0")
    plan = build_settings_plan(batch4["row"], track_idx=track_idx)
    rows = []
    batch_results = []
    all_ok = True
    restored = False
    try:
        for batch in plan:
            res = run_script_playbook(tool, batch["script"], build, max_underruns, timeout,
                                      expect_card=had_card or expect_card, elf_dir=elf_dir)
            had_card = had_card or res["had_card"]
            measures = res.get("measures", [])
            batch_ok = res["ok"] and len(measures) == len(batch["labels"])
            for label, m in zip(batch["labels"], measures):
                und = m.get("underruns") or 0
                out_und = m.get("output_underruns") or 0
                active = _measure_audio_active(m)
                blocks = m.get("blocks") or 0
                row_ok = res["ok"] and active and blocks > 0 and und <= max_underruns and out_und <= max_underruns
                batch_ok = batch_ok and row_ok
                rows.append({
                    "batch": batch["name"],
                    "label": label,
                    "frames": m.get("frames"),
                    "frame_avg_us": m.get("frame_avg_us"),
                    "frame_worst_us": m.get("frame_worst_us"),
                    "draw_avg_us": m.get("draw_avg_us"),
                    "send_avg_us": m.get("send_avg_us"),
                    "core0_pct": m.get("core0_busy_percent"),
                    "core1_pct": _measure_core1_percent(m),
                    "audio_busy_pct": m.get("audio_busy_percent"),
                    "audio": 1 if active else 0,
                    "buffered_ms": m.get("buffered_ms"),
                    "read_max_us": m.get("read_max_us"),
                    "handoff_max_us": m.get("handoff_max_us"),
                    "underruns": und,
                    "output_underruns": out_und,
                    "ok": row_ok,
                })
            all_ok = all_ok and batch_ok
            batch_results.append({
                "name": batch["name"],
                "ok": batch_ok,
                "script_state": res["script_state"],
                "bad_lines": res["bad_lines"],
            })
    finally:
        restore_script = build_settings_restore_script(owner_settings)
        restore_res = run_script_playbook(tool, restore_script, build, max_underruns, timeout,
                                          expect_card=had_card or expect_card, elf_dir=elf_dir)
        restored = bool(restore_res["ok"])
    return {
        "ok": all_ok and restored,
        "had_card": had_card,
        "track": track_name,
        "owner_settings": owner_settings,
        "restored": restored,
        "batches": batch_results,
        "rows": rows,
    }


def run_playbook(name, tool, build, rates, bits, silent, max_underruns, max_dac_gap_us, timeout,
                 streamer=stream_to_dac, elf_dir=None, bits_list=None):
    if name == "all":
        selected = ("storage", "ui", "playback", "rescan", "dac")
    elif name == "certify":
        selected = ("storage", "codecs", "settings", "ui", "rescan", "dac")
    else:
        selected = (name,)
    dac_bits = bits_list if bits_list else ([16, 24, 32] if name == "certify" else [bits])
    results = {}
    had_card = False
    for step in selected:
        if step == "storage":
            res = run_storage_playbook(tool)
            had_card = had_card or res["had_card"]
        elif step == "codecs":
            res = run_codecs_playbook(tool, build, max_underruns, timeout,
                                      expect_card=had_card, elf_dir=elf_dir)
            had_card = had_card or res["had_card"]
        elif step == "settings":
            res = run_settings_playbook(tool, build, max_underruns, timeout,
                                        expect_card=had_card, elf_dir=elf_dir)
            had_card = had_card or res["had_card"]
        elif step == "ui":
            res = run_script_playbook(tool, PLAYBOOK_UI_SCRIPT, build, max_underruns, timeout,
                                      expect_card=had_card, elf_dir=elf_dir)
            had_card = had_card or res["had_card"]
            rtc_ok = all(entry.get("status") == "ok" for entry in res["rtc"]) if res["rtc"] else False
            res["rtc_ok"] = rtc_ok
            res["ok"] = res["ok"] and rtc_ok
        elif step == "playback":
            res = run_script_playbook(tool, PLAYBOOK_PLAYBACK_SCRIPT, build, max_underruns, timeout,
                                      expect_card=had_card, elf_dir=elf_dir)
            had_card = had_card or res["had_card"]
            zero_underruns = all(
                (m.get("underruns", 0) or 0) <= max_underruns
                and (m.get("output_underruns", 0) or 0) <= max_underruns
                for m in res["measures"]
            )
            res["ok"] = res["ok"] and zero_underruns
        elif step == "rescan":
            res = run_script_playbook(tool, PLAYBOOK_RESCAN_SCRIPT, build, max_underruns, timeout,
                                      expect_card=had_card, elf_dir=elf_dir)
            had_card = had_card or res["had_card"]
            res["scan"] = extract_scan_timings(res["events"])
        elif step == "dac":
            runs = []
            for depth in dac_bits:
                for rate in rates:
                    run = run_dac_test(
                        tool, build, rate, silent, max_underruns, max_dac_gap_us, timeout,
                        bits=depth, streamer=streamer, expect_card=had_card, allow_prior_drops=True,
                        elf_dir=elf_dir,
                    )
                    had_card = had_card or run["had_card"]
                    runs.append(run)
            res = {"ok": all(r["ok"] for r in runs), "runs": runs}
        else:
            raise ValueError(f"unknown playbook {step!r}")
        results[step] = res
    return {"ok": all(r["ok"] for r in results.values()), "playbooks": results}


def print_playbook_summary(result):
    playbooks = result["playbooks"]
    if "storage" in playbooks:
        st = playbooks["storage"]
        seq, br = st["sequential"], st["burst"]
        print(f"STORAGE  {'PASS' if st['ok'] else 'FAIL'} "
              f"seq({seq['bytes'] // (1024 * 1024)} MiB) w={seq['write_mb_s']} MB/s r={seq['read_mb_s']} MB/s | "
              f"burst({br['count']}x{br['bytes_each'] // 1024} KiB) w={br['write_mb_s']} MB/s r={br['read_mb_s']} MB/s")
        if st.get("usb_timing"):
            print(f"         usb.timing: {st['usb_timing']}")
    if "codecs" in playbooks:
        cd = playbooks["codecs"]
        passed = sum(1 for row in cd.get("tracks", []) if row["ok"])
        total = len(cd.get("tracks", []))
        print(f"\nCODECS   {'PASS' if cd['ok'] else 'FAIL'} ({passed}/{total} tracks)")
        print(f"  {'Track':<36} {'Aud':>3} {'Buf':>6} {'C0%':>4} {'C1%':>5} {'Aud%':>4} {'RdMax':>7} {'HoMax':>7} {'Und':>5} {'Res':>4}")
        for row in cd.get("tracks", []):
            c1 = f"{row['core1_pct']:.1f}" if row["core1_pct"] is not None else "-"
            rd = f"{row['read_max_us']}us" if row["read_max_us"] is not None else "-"
            ho = f"{row['handoff_max_us']}us" if row["handoff_max_us"] is not None else "-"
            buf = f"{row['buffered_ms']}ms" if row["buffered_ms"] is not None else "-"
            print(
                f"  {row['label']:<36} {row['audio']:>3} {buf:>6} "
                f"{str(row['core0_pct'] if row['core0_pct'] is not None else '-'):>4} {c1:>5} "
                f"{str(row['audio_busy_pct'] if row['audio_busy_pct'] is not None else '-'):>4} "
                f"{rd:>7} {ho:>7} {row['underruns']}/{row['output_underruns']:<3} "
                f"{'PASS' if row['ok'] else 'FAIL':>4}"
            )
    if "settings" in playbooks:
        sg = playbooks["settings"]
        passed = sum(1 for row in sg.get("rows", []) if row["ok"])
        total = len(sg.get("rows", []))
        print(f"\nSETTINGS {'PASS' if sg['ok'] else 'FAIL'} ({passed}/{total} configs, restored={sg.get('restored')}, track={sg.get('track')})")
        print(f"  {'Configuration':<44} {'Fr':>3} {'Draw':>8} {'Worst':>8} {'C0%':>4} {'C1%':>5} {'Aud%':>4} {'Buf':>6} {'Und':>5} {'Res':>4}")
        for row in sg.get("rows", []):
            c1 = f"{row['core1_pct']:.1f}" if row["core1_pct"] is not None else "-"
            dr = f"{row['draw_avg_us']}us" if row["draw_avg_us"] is not None else "-"
            wr = f"{row['frame_worst_us']}us" if row["frame_worst_us"] is not None else "-"
            buf = f"{row['buffered_ms']}ms" if row["buffered_ms"] is not None else "-"
            print(
                f"  {row['label']:<44} {str(row['frames'] if row['frames'] is not None else '-'):>3} "
                f"{dr:>8} {wr:>8} {str(row['core0_pct'] if row['core0_pct'] is not None else '-'):>4} {c1:>5} "
                f"{str(row['audio_busy_pct'] if row['audio_busy_pct'] is not None else '-'):>4} {buf:>6} "
                f"{row['underruns']}/{row['output_underruns']:<3} {'PASS' if row['ok'] else 'FAIL':>4}"
            )
    for key, label in (("ui", "UI      "), ("playback", "PLAYBACK"), ("rescan", "RESCAN  ")):
        if key not in playbooks:
            continue
        res = playbooks[key]
        extra = ""
        if key == "ui":
            extra = f" rtc={'ok' if res.get('rtc_ok') else 'fail'} shots={len(res.get('screenshots', []))}"
        elif key == "rescan":
            dur = res.get("scan", {}).get("duration_ms")
            extra = f" scan_ms={dur if dur is not None else 'n/a'}"
        print(f"{label} {'PASS' if res['ok'] else 'FAIL'} state={res['script_state']} "
              f"bad_lines={len(res['bad_lines'])}{extra}")
        for idx, m in enumerate(res.get("measures", []), 1):
            aud = 1 if _measure_audio_active(m) else 0
            c1 = _measure_core1_percent(m)
            c1_txt = f"{c1}%" if c1 is not None else "n/a"
            print(f"         win{idx}: frames={m.get('frames')} avg={m.get('frame_avg_us')}us "
                  f"worst={m.get('frame_worst_us')}us draw={m.get('draw_avg_us')}us send={m.get('send_avg_us')}us "
                  f"core0={m.get('core0_busy_percent')}% core1={c1_txt} "
                  f"audio={aud} buf={m.get('buffered_ms')}ms "
                  f"read_max={m.get('read_max_us')}us underruns={m.get('underruns')}/{m.get('output_underruns')}")
        top = res.get("profile_top", [])[:5]
        if top:
            formatted = ", ".join(f"{fn['name']} {fn['percent']}%" for fn in top)
            print(f"         profile: {formatted}")
    if "dac" in playbooks:
        print_dac_summary_table(playbooks["dac"]["runs"])


def run_local_smoke(tool, build, max_underruns, timeout, expect_card=False, elf_dir=None):
    res = run_script_playbook(tool, LOCAL_SMOKE_SCRIPT, build, max_underruns, timeout,
                              expect_card=expect_card, elf_dir=elf_dir)
    return {"ok": res["ok"], "had_card": res["had_card"], "script_state": res["script_state"],
            "bad_lines": res["bad_lines"], "scorecard": res["scorecard"]}


def run_suite(tool, build, matrix, silent, max_underruns, max_dac_gap_us, timeout,
              streamer=stream_to_dac, elf_dir=None):
    storage = run_storage_transfer_check(tool)
    local = run_local_smoke(tool, build, max_underruns, timeout, elf_dir=elf_dir)
    had_card = local["had_card"]
    dac_runs = []
    for rate, bits in matrix:
        result = run_dac_test(
            tool, build, rate, silent, max_underruns, max_dac_gap_us, timeout,
            bits=bits, streamer=streamer, expect_card=had_card, allow_prior_drops=True,
            elf_dir=elf_dir,
        )
        had_card = had_card or result["had_card"]
        dac_runs.append(result)
    ok = storage["ok"] and local["ok"] and all(run["ok"] for run in dac_runs)
    return {"ok": ok, "storage": storage, "local": local, "dac_runs": dac_runs}


def print_dac_summary_table(runs):
    print("\nDAC Sweep Summary:")
    print(f"  {'Rate':>7} {'Bits':>4} {'Packets':>8} {'Under':>5} {'Over':>4} {'Realg':>5} {'MaxGap':>8} {'Result':>6}  MaxGapPC")
    for run in runs:
        vals = run["scorecard"].get("values", {})
        pc_check = next((c["value"] for c in run["scorecard"]["checks"]
                         if c["name"] == "usb.dac.max_gap_pc"), "-")
        print(
            f"  {run['rate']:>7} {vals.get('usb.dac.bits', run['bits'] or '-'):>4} "
            f"{str(vals.get('usb.dac.packets', '-')):>8} "
            f"{str(vals.get('usb.dac.underruns', '-')):>5} "
            f"{str(vals.get('usb.dac.overflows', '-')):>4} "
            f"{str(vals.get('usb.dac.realigned', '-')):>5} "
            f"{str(vals.get('usb.dac.max_gap_us', '-')) + 'us':>8} "
            f"{'PASS' if run['ok'] else 'FAIL':>6}  {pc_check}"
        )


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check", help="score the newest debug report by sequence")
    check.add_argument("--volume", type=Path, help="mounted or extracted internal storage folder")
    check.add_argument("--build", metavar="SHA7", type=sha7, help="expected firmware commit")
    check.add_argument("--max-underruns", type=nonnegative_int, default=0,
                       help="highest passing output underrun count (default %(default)s)")
    check.add_argument("--require-dac", action="store_true",
                       help="require usb.dac.* stats from a completed USB DAC session")
    check.add_argument("--max-dac-gap-us", type=nonnegative_int,
                       help="optional upper bound on usb.dac.max_gap_us")
    check.add_argument("--elf-dir", type=Path, help="directory holding echo_device.elf / echo_usb_overlay.elf")
    check.add_argument("--json", action="store_true", help="emit the scorecard as JSON")

    dac = commands.add_parser("dac-test", help="trigger automated USB DAC menu test, stream audio, and score report")
    rate_group = dac.add_mutually_exclusive_group()
    rate_group.add_argument("--rate", type=int, choices=VALID_RATES,
                            help="single host sample rate to stream to the USB DAC (default 96000)")
    rate_group.add_argument("--rates", type=parse_rates,
                            help="comma-separated sample rates to sweep, e.g. 48000,96000,192000")
    dac.add_argument("--bits", type=int, default=0, choices=VALID_BITS,
                     help="physical USB Audio bit depth to request (0 = host default, 16, 24, or 32)")
    dac.add_argument("--silent", action="store_true", help="stream digital silence instead of a quiet -42 dBFS tone")
    dac.add_argument("--build", metavar="SHA7", type=sha7, help="expected firmware commit")
    dac.add_argument("--max-underruns", type=nonnegative_int, default=0,
                     help="highest passing output underrun count (default %(default)s)")
    dac.add_argument("--max-dac-gap-us", type=nonnegative_int,
                     help="optional upper bound on usb.dac.max_gap_us")
    dac.add_argument("--timeout", type=int, default=45, help="seconds to wait for reconnect (default %(default)s)")
    dac.add_argument("--elf-dir", type=Path, help="directory holding echo_device.elf / echo_usb_overlay.elf")
    dac.add_argument("--json", action="store_true", help="emit the result as JSON")

    suite = commands.add_parser("suite", help="run full hands-free hardware validation: storage, local UI, and DAC matrix")
    suite.add_argument("--rates", type=parse_rates, default=[48000, 96000, 192000],
                       help="comma-separated DAC rates for 32-bit sweep (default 48000,96000,192000)")
    suite.add_argument("--bits", type=int, default=32, choices=VALID_BITS,
                       help="bit depth for DAC sweep (default 32)")
    suite.add_argument("--silent", action="store_true", help="stream digital silence during DAC checks")
    suite.add_argument("--build", metavar="SHA7", type=sha7, help="expected firmware commit")
    suite.add_argument("--max-underruns", type=nonnegative_int, default=0,
                       help="highest passing output underrun count (default %(default)s)")
    suite.add_argument("--max-dac-gap-us", type=nonnegative_int,
                       help="optional upper bound on usb.dac.max_gap_us")
    suite.add_argument("--timeout", type=int, default=45, help="seconds to wait per reconnect (default %(default)s)")
    suite.add_argument("--elf-dir", type=Path, help="directory holding echo_device.elf / echo_usb_overlay.elf")
    suite.add_argument("--json", action="store_true", help="emit the result as JSON")

    playbook = commands.add_parser("playbook", help="run repeatable real-hardware validation playbooks")
    playbook.add_argument("name", choices=PLAYBOOK_NAMES,
                          help="playbook to run: storage, codecs, settings, ui, playback, rescan, dac, all, or certify")
    playbook.add_argument("--rates", type=parse_rates, default=[44100, 48000, 96000, 192000],
                          help="DAC rates when running dac/all/certify (default 44100,48000,96000,192000)")
    playbook.add_argument("--bits", type=int, default=32, choices=VALID_BITS,
                          help="DAC bit depth when running dac/all (default 32)")
    playbook.add_argument("--bits-list", type=parse_bits_list,
                          help="comma-separated DAC bit depths to sweep, e.g. 16,24,32")
    playbook.add_argument("--silent", action="store_true", help="stream digital silence during DAC checks")
    playbook.add_argument("--build", metavar="SHA7", type=sha7, help="expected firmware commit")
    playbook.add_argument("--max-underruns", type=nonnegative_int, default=0,
                          help="highest passing output underrun count (default %(default)s)")
    playbook.add_argument("--max-dac-gap-us", type=nonnegative_int, default=200,
                          help="upper bound on usb.dac.max_gap_us (default %(default)s)")
    playbook.add_argument("--timeout", type=int, default=60, help="seconds to wait per reconnect (default %(default)s)")
    playbook.add_argument("--elf-dir", type=Path, help="directory holding echo_device.elf / echo_usb_overlay.elf")
    playbook.add_argument("--save", type=Path, nargs="?", const=PLAYBOOK_REPORT_DIR,
                          help="save full JSON report to this file or directory (default Scratch/reports/hw-playbooks)")
    playbook.add_argument("--json", action="store_true", help="emit the result as JSON")

    script = commands.add_parser("soak-script", help="generate a script for a paused track on an English UI")
    script.add_argument("--output", type=Path, help="save to this computer instead of printing the script")
    script.add_argument("--build", metavar="SHA7", type=sha7,
                        help="bind the script to this firmware commit (defaults to any build)")
    commands.add_parser("checklist", help="print the manual on-player release checks")
    return parser


def main(argv=None, host=None, streamer=stream_to_dac):
    args = build_parser().parse_args(argv)
    if args.command == "check":
        result = check_report(echo_debug.Tool(host=host, volume=args.volume), args.build,
                              args.max_underruns, require_dac=args.require_dac,
                              max_dac_gap_us=args.max_dac_gap_us, elf_dir=args.elf_dir)
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            print_scorecard(result)
        return 0 if result["ok"] else 1
    if args.command == "dac-test":
        tool = echo_debug.Tool(host=host)
        rates = args.rates if args.rates is not None else [args.rate or 96000]
        if len(rates) == 1:
            result = run_dac_test(tool, args.build, rates[0], args.silent, args.max_underruns,
                                  args.max_dac_gap_us, args.timeout, bits=args.bits,
                                  streamer=streamer, elf_dir=args.elf_dir)
            if args.json:
                print(json.dumps(result, indent=2))
            else:
                stream = result["stream"]
                print(f"STREAM {'OK' if stream.get('ok') else 'FAIL'} {json.dumps(stream)}")
                print_scorecard(result["scorecard"])
            return 0 if result["ok"] else 1
        runs = []
        had_card = False
        for index, rate in enumerate(rates):
            run = run_dac_test(tool, args.build, rate, args.silent, args.max_underruns,
                               args.max_dac_gap_us, args.timeout, bits=args.bits,
                               streamer=streamer, expect_card=had_card,
                               allow_prior_drops=True, elf_dir=args.elf_dir)
            had_card = had_card or run["had_card"]
            runs.append(run)
        result = {"ok": all(run["ok"] for run in runs), "runs": runs}
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            print_dac_summary_table(runs)
        return 0 if result["ok"] else 1
    if args.command == "suite":
        tool = echo_debug.Tool(host=host)
        matrix = [(rate, args.bits) for rate in args.rates]
        result = run_suite(tool, args.build, matrix, args.silent, args.max_underruns,
                           args.max_dac_gap_us, args.timeout, streamer=streamer, elf_dir=args.elf_dir)
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            st = result["storage"]
            print(f"STORAGE {'PASS' if st['ok'] else 'FAIL'} {st['bytes'] // 1024} KiB "
                  f"write={st['write_mb_s']} MB/s read={st['read_mb_s']} MB/s sha256={st['sha256'][:12]}")
            loc = result["local"]
            print(f"LOCAL   {'PASS' if loc['ok'] else 'FAIL'} script.state={loc['script_state']}")
            print_dac_summary_table(result["dac_runs"])
        return 0 if result["ok"] else 1
    if args.command == "playbook":
        tool = echo_debug.Tool(host=host)
        result = run_playbook(args.name, tool, args.build, args.rates, args.bits, args.silent,
                              args.max_underruns, args.max_dac_gap_us, args.timeout,
                              streamer=streamer, elf_dir=args.elf_dir, bits_list=args.bits_list)
        if args.save is not None:
            dest = args.save
            if dest.is_dir() or not dest.suffix:
                dest.mkdir(parents=True, exist_ok=True)
                stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
                tag = args.build or "live"
                dest = dest / f"{stamp}-{args.name}-{tag}.json"
            else:
                dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            result["saved_to"] = str(dest)
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            print_playbook_summary(result)
            if "saved_to" in result:
                print(f"\nSaved JSON report: {result['saved_to']}")
        return 0 if result["ok"] else 1
    if args.command == "soak-script":
        try:
            data = echo_debug.prepare_script(SOAK_SCRIPT, args.build or "any")
            if args.output is None:
                print(data.decode("utf-8"), end="")
            else:
                args.output.write_bytes(data)
        except (echo_debug.DebugError, OSError) as error:
            print(f"FAIL soak-script: {error}", file=sys.stderr)
            return 1
    else:
        print(CHECKLIST, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
