import argparse
import re
import sys
from bisect import bisect_right
from itertools import accumulate
from pathlib import Path

BOARD = Path("platform/device/board")
ALLOWLIST = {
    (BOARD / "audio.c", "board_audio_vorbis_wait"): "Decoder prefill pumps its own source",
    (BOARD / "clock_switch.c", "run_from_pll"): "PLL divider settle during clock switching",
    (BOARD / "core1.c", "bus_delay_ms"): "Core 1 power, reset, and clock sequencing",
    (BOARD / "debug_rtc.c", "open_bus"): "200 us I2C reset pulse",
    (BOARD / "emmc.c", "emmc_reset_controller"): "10 us eMMC reset pulse",
    (BOARD / "emmc.c", "identify"): "100 us eMMC settle after GO_IDLE",
    (BOARD / "emmc.c", "power_controller"): "10 us eMMC pin mux settle",
    (BOARD / "input.c", "board_battery_boot_reading"): "Boot battery ADC conversion",
    (BOARD / "main.c", "switch_off_when_empty"): "Low battery notice before boot power-off",
    (BOARD / "mmc.c", "mmc_power_cycle"): "10 us power-off and 600 us power-on settle",
    (BOARD / "panel_vop.c", "bus_delay"): "Panel initialization table at boot",
    (BOARD / "panel_vop.c", "power_controller"): "Display controller reset and settle at boot",
    (BOARD / "panel_vop.c", "pulse_reset"): "Panel reset line sequence at boot",
    (BOARD / "power.c", "hal_power_off"): "DAC discharge after stopping audio",
    (BOARD / "sd.c", "sd_reset_controller"): "10 us SD reset pulse",
    (BOARD / "usb.c", "reset_core"): "USB clock gate settle during disconnected reset",
    (BOARD / "usb.c", "usb_connect"): "Hold soft disconnect before reconnect",
    (BOARD / "usb.c", "usb_power_down"): "Hold soft disconnect during power-down",
    (BOARD / "usb.c", "usb_power_up"): "USB PHY reset and power-on settle",
    (BOARD / "watchdog.c", "board_watchdog_start"): "1000 us watchdog reset pulse",
}

NON_CODE = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
DIRECTIVE = re.compile(r"^[ \t]*#[^\n]*", re.MULTILINE)
BRACE = re.compile(r"[{}]")
NAME_BEFORE_ARGUMENTS = re.compile(r"\b([A-Za-z_]\w*)\s*\Z")
RAW_DELAY = re.compile(r"\bboard_delay_(?:ms|us)(?=\s*\()")


def blank(match):
    return re.sub(r"[^\n]", " ", match[0])


def function_before(code, brace):
    head = code[:brace].rstrip()
    if not head.endswith(")"):
        return None
    nesting = 0
    for index in range(len(head) - 1, -1, -1):
        if head[index] == ")":
            nesting += 1
        elif head[index] == "(":
            nesting -= 1
            if nesting == 0:
                return NAME_BEFORE_ARGUMENTS.search(head, 0, index)
    return None


def function_bodies(code):
    code = DIRECTIVE.sub(blank, code)
    depth = 0
    for brace in BRACE.finditer(code):
        if brace[0] == "{":
            if depth == 0:
                start = brace.start()
                name = function_before(code, start)
            depth += 1
        else:
            depth -= 1
            if depth == 0 and name is not None:
                yield name, start, brace.end()


def delay_sites(source):
    chunks = source.split("\\\n")
    splice_offsets = list(accumulate(map(len, chunks[:-1])))
    code = NON_CODE.sub(blank, "".join(chunks))
    bodies = iter(function_bodies(code))
    body = next(bodies, None)
    for delay in RAW_DELAY.finditer(code):
        offset = delay.start()
        while body is not None and offset >= body[2]:
            body = next(bodies, None)
        function = "<file scope>"
        if body is not None:
            name, start, _ = body
            if offset == name.start(1):
                continue
            if offset > start:
                function = name[1]
        line = code.count("\n", 0, offset) + bisect_right(splice_offsets, offset) + 1
        yield function, line, delay[0]


def find_violations(sources, allowlist):
    violations = []
    used = set()
    for path, source in sources:
        for function, line, delay in delay_sites(source):
            site = path, function
            if site in allowlist:
                used.add(site)
            else:
                violations.append(f"{path}:{function}:{line}: unserviced {delay} call")
    for path, function in sorted(allowlist.keys() - used):
        violations.append(f"{path}:{function}: unused raw delay allowance ({allowlist[path, function]})")
    return violations


def self_test():
    path = BOARD / "example.c"
    source = """#define BAD_DELAY() \\
    { board_delay_\\
us(2); }
CORE_COLD void boot(void (*callback)(void))
{
    if (callback) { board_delay_us(1); }
}
void runtime(void)
{
    const char *text = "} board_delay_ms(3)";
    board_delay_ms
        (1);
}
void board_delay_us(unsigned duration)
{
}
"""
    violations = find_violations([(path, source)], {(path, "boot"): "reset", (path, "old"): "boot"})
    expected = [
        f"{path}:<file scope>:2: unserviced board_delay_us call",
        f"{path}:runtime:11: unserviced board_delay_ms call",
        f"{path}:old: unused raw delay allowance (boot)",
    ]
    if violations != expected:
        raise AssertionError(f"Delay scanner returned {violations!r}, expected {expected!r}")


def main():
    parser = argparse.ArgumentParser(description="Fail when a board function adds an unserviced delay")
    parser.add_argument("root", type=Path)
    root = parser.parse_args().root.resolve()
    self_test()
    sources = (
        (path.relative_to(root), path.read_text(encoding="utf-8"))
        for path in sorted((root / BOARD).glob("*.c"))
    )
    violations = find_violations(sources, ALLOWLIST)
    for violation in violations:
        print(violation)
    print(f"{len(violations)} violation(s), {len(ALLOWLIST)} allowances")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
