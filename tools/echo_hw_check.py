#!/usr/bin/env python3
"""Check the player's debug report and run automated on-player hardware validation."""

import argparse
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
STORAGE_TEST_FILE = "xfer.tmp"

DAC_TEST_SCRIPT = """\
reconnect dac
"""

LOCAL_SMOKE_SCRIPT = """\
open home
wait 400
open settings
wait 400
open home
wait 400
reconnect
"""

COREAUDIO_STREAM_SWIFT = r"""
import AudioToolbox
import CoreAudio
import Foundation

let args = CommandLine.arguments
let names = args.count > 1 ? args[1].split(separator: "|").map(String.init) : ["Gnarlyr Echo DAC"]
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

var formatStatus: OSStatus = 0
var selectedBits: UInt32 = 0
if wantedBits > 0 {
    let res = configurePhysicalFormat(deviceID, rate: rate, bits: wantedBits)
    formatStatus = res.0
    selectedBits = res.1
}
var targetRate = rate
var rateAddr = AudioObjectPropertyAddress(
    mSelector: kAudioDevicePropertyNominalSampleRate,
    mScope: kAudioObjectPropertyScopeGlobal,
    mElement: kAudioObjectPropertyElementMain)
let rateStatus = AudioObjectSetPropertyData(deviceID, &rateAddr, 0, nil, UInt32(MemoryLayout<Float64>.size), &targetRate)
Thread.sleep(forTimeInterval: 0.2)
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
        check("battery.millivolts", millivolts, "unknown or 3000..4500 mV",
              millivolts is None or isinstance(millivolts, int) and 3000 <= millivolts <= 4500)
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


def stream_to_dac(rate, amplitude, connect_timeout, stream_timeout, bits=0):
    names = "|".join([echo_debug.IDENTITY.get("usb_dac_name", "Gnarlyr Echo DAC"),
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


def wait_for_settled_luns(tool, expect_card=False, settle_seconds=4.0):
    """Wait until any attached SD LUN finishes mounting on macOS before ejecting."""
    found = tool.locate()
    if not expect_card or not found.get("usb") or found["usb"].get("card_volumes"):
        return found
    deadline = tool.clock() + settle_seconds
    while tool.clock() < deadline:
        tool.sleep(0.25)
        found = tool.locate()
        if found.get("usb") and found["usb"].get("card_volumes"):
            return found
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


def run_storage_transfer_check(tool, size_bytes=STORAGE_TEST_BYTES):
    root = tool.root()
    target = root / echo_debug.DATA_DIRECTORY / echo_debug.DEBUG_DIRECTORY / STORAGE_TEST_FILE
    payload = os.urandom(size_bytes)
    expected = hashlib.sha256(payload).hexdigest()
    started = time.monotonic()
    with target.open("wb") as handle:
        handle.write(payload)
        handle.flush()
        os.fsync(handle.fileno())
    write_s = max(time.monotonic() - started, 1e-6)
    read_start = time.monotonic()
    actual = hashlib.sha256(target.read_bytes()).hexdigest()
    read_s = max(time.monotonic() - read_start, 1e-6)
    target.unlink()
    sidecar = target.with_name(f"._{target.name}")
    if sidecar.exists():
        sidecar.unlink()
    mb = size_bytes / (1024 * 1024)
    return {
        "ok": actual == expected,
        "bytes": size_bytes,
        "sha256": actual,
        "write_mb_s": round(mb / write_s, 2),
        "read_mb_s": round(mb / read_s, 2),
    }


def run_local_smoke(tool, build, max_underruns, timeout, expect_card=False, elf_dir=None):
    found = wait_for_settled_luns(tool, expect_card=expect_card)
    had_card = bool(found.get("usb") and found["usb"].get("card_volumes"))
    root = Path(found["volume"]) if found.get("volume") else tool.root()
    target_build = build or tool.read_report()[1]["values"].get("firmware.build") or "any"
    data = echo_debug.prepare_script(LOCAL_SMOKE_SCRIPT, target_build)
    tool.write(root, root / echo_debug.DATA_DIRECTORY / echo_debug.DEBUG_DIRECTORY / echo_debug.RUN_FILE, data)
    tool.eject(wait_for_settled_luns(tool, expect_card=had_card or expect_card))
    tool.wait_for_return(timeout)
    wait_for_settled_luns(tool, expect_card=had_card or expect_card)
    scorecard = check_report(tool, build, max_underruns, max_dropped_events=None, elf_dir=elf_dir)
    script_state = scorecard["values"].get("script.state")
    ok = scorecard["ok"] and script_state == "finished"
    return {"ok": ok, "had_card": had_card, "script_state": script_state, "scorecard": scorecard}


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
                            help="single host sample rate to stream to Gnarlyr Echo DAC (default 96000)")
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
                               allow_prior_drops=index > 0, elf_dir=args.elf_dir)
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
