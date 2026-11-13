import argparse
import copy
import json
import struct
from pathlib import Path
from types import SimpleNamespace

import image_artifacts
import make_image
from identity import IDENTITY

CHUNK_SECTORS = 16
SLOT_SECTORS = 0x2800
TRIAL_PATH = "/" + IDENTITY.data_directory + "/trial.bin"
TRIAL_MAGIC = b"DSUT"
TRIAL_RUNNING = 1
TRIAL_ROLLED_BACK = 3
TRIAL_LIMIT = 3


def require(condition, message):
    if not condition:
        raise ValueError(message)


def healthy(session):
    require(not session.machine.faults and not session.machine.panics,
            f"Emulator failure: {session.machine.faults}, {session.machine.panics}")


def slot_spans(session):
    slots = {name: session.layout[name] for name in ("slot_1", "slot_2")}
    return slots, {name: range(start, start + SLOT_SECTORS) for name, start in slots.items()}


def verify_permitted(session):
    _, inside = slot_spans(session)
    volume_start = session.layout["volume_start"]
    volume_end = volume_start + session.layout["volume_sectors"]
    for _, lba, count in session.card.writes:
        require((volume_start <= lba and lba + count <= volume_end)
                or (count == CHUNK_SECTORS and any(lba in span and lba + count <= span.stop
                                                  for span in inside.values())), "Write outside permitted regions")
    for lba in session.changed_sectors():
        require(volume_start <= lba < volume_end or any(lba in span for span in inside.values()),
                "Changed sector outside permitted regions")
    return [item for item in session.card.writes if item[1] < volume_start]


def verify_readbacks(session):
    volume_start = session.layout["volume_start"]
    accesses = [item for item in session.card.accesses if item[0] != "W" or item[1] < volume_start]
    for index, access in enumerate(accesses):
        if access[0] != "W":
            continue
        lba, count = access[1:3]
        covered = []
        for later in accesses[index + 1:]:
            if later[0] != "R" or len(covered) >= count:
                break
            covered.extend(range(later[1], later[1] + later[2]))
        require(covered[:count] == list(range(lba, lba + count)), "Write was not immediately read back")
    return accesses


def verify_slot_written(session, accesses, name, image):
    slots, inside = slot_spans(session)
    start = slots[name]
    word = struct.unpack_from("<I", image)[0]
    sequence = [(lba, count, value) for kind, lba, count, *rest in accesses
                if kind == "W" and lba in inside[name] for value in rest]
    require(sequence[0] == (start, CHUNK_SECTORS, 0xFFFFFFFF), "Slot header must be invalidated first")
    require(sequence[-1] == (start, CHUNK_SECTORS, word), "Slot header must be committed last")
    require(start not in [lba for lba, _, _ in sequence[1:-1]], "Early header write")
    require(session.slot_image(name) == image[:make_image.fw_end(make_image.FW_SIZE)],
            f"{name} does not match the exact new image")


def verify_transaction(session, newer):
    _, inside = slot_spans(session)
    writes = verify_permitted(session)
    by_slot = {name: [item for item in writes if item[1] in span] for name, span in inside.items()}
    require(all(by_slot.values()), "Both slots must be written")
    require(max(stamp for stamp, _, _ in by_slot["slot_2"])
            < min(stamp for stamp, _, _ in by_slot["slot_1"]), "Slot 2 must finish before slot 1")
    accesses = verify_readbacks(session)
    for name in inside:
        verify_slot_written(session, accesses, name, newer)
    return len(writes)


def verify_single_slot(session, written, kept, image, kept_image):
    _, inside = slot_spans(session)
    writes = verify_permitted(session)
    require(writes and all(item[1] in inside[written] for item in writes),
            f"Only {written} may be written")
    verify_slot_written(session, verify_readbacks(session), written, image)
    require(session.slot_image(kept) == kept_image[:make_image.fw_end(make_image.FW_SIZE)],
            f"{kept} must keep the known-good image")
    return len(writes)


def trial_record(session):
    reader = session.fat()
    if not reader.exists(TRIAL_PATH):
        return None
    data = reader.read(TRIAL_PATH)
    require(data[:4] == TRIAL_MAGIC and data[7] == 1 and data[6] == data[4] ^ data[5] ^ 0x5A,
            "Damaged trial record")
    return data[4], data[5]


INSTALLS = ("updater_install", "updater_install_trial", "trial_module_install")


def trial_install_name(directory):
    from echo_emulator.machine import load_symbols

    extension = directory / "platform/device/echo_module_extension.elf"
    if extension.is_file() and "trial_module_install" in load_symbols(extension):
        return "trial_module_install"
    overlay = directory / "platform/device/echo_usb_overlay.elf"
    return "updater_install_trial" if "updater_install_trial" in load_symbols(overlay) else None


def has_trial(directory):
    return trial_install_name(directory) is not None


def forked(session):
    return SimpleNamespace(disk=copy.deepcopy(session.disk), layout=session.layout,
                           volume=session.volume, machine=session.machine)


def candidate_session(directory, files=(), reuse=None, historical=False, vbus=None):
    from echo_emulator.session import Session

    symbols = directory / "platform/device"
    image = (directory / IDENTITY.update_file).read_bytes()
    require(not make_image.validate_image(image), "Candidate image validation failed")
    firmware = make_image.firmware_from_elf((symbols / "echo_device.elf").read_bytes())
    require(image[make_image.CODE_OFFSET:make_image.CODE_OFFSET + len(firmware.code)] == firmware.code,
            "Resident ELF code does not belong to the image")
    data_offset = make_image.CODE_OFFSET + len(firmware.code)
    require(image[data_offset:data_offset + len(firmware.data)] == firmware.data,
            "Resident ELF data does not belong to the image")
    return Session(image, symbols / "echo_device.elf",
                   files=files, reuse=reuse,
                   vbus_after_first_frame=bool(files) if vbus is None else vbus,
                   usb_overlay_elf=symbols / "echo_usb_overlay.elf",
                   module_elfs=image_artifacts.module_elfs(directory),
                   optional_hooks=("hal_system_set_usb_answer",) if historical else ())


def install(old, newer):
    session = candidate_session(old, files=[(IDENTITY.update_file, newer, None)], historical=True)
    for name in INSTALLS:
        session.machine.note(name, optional=True)
    require(session.run_to_first_frame() == "condition met", "Old image did not boot")
    session.machine.stop_at("board_wait_for_power_hold")
    require(session.run_until_waiting_for_key(presses=1) == "reached board_wait_for_power_hold",
            "Old updater did not finish")
    healthy(session)
    require(session.machine.usb_connect_count >= 1, "USB transfer did not run")
    return session


def run_until_promoted(session, step_ms=500, limit_ms=30000):
    machine = session.machine
    machine.continue_past_stop()
    end = machine.milliseconds() + limit_ms
    while machine.milliseconds() < end:
        machine.run(until_ms=machine.milliseconds() + step_ms)
        if machine.faults or machine.panics or trial_record(session) is None:
            break
    return trial_record(session) is None


def prove_confirm(state, new, newer):
    session = candidate_session(new, reuse=forked(state), vbus=True)
    session.machine.host_ejects()
    session.machine.stub_symbol("msc_write_proven", lambda machine: 1)
    require(session.run_to_first_frame() == "condition met", "Trial image did not boot")
    require(trial_record(session) == (TRIAL_RUNNING, 1), "Trial start was not counted")
    require(session.run_through_update_check() == "reached usb_session_storage_changed",
            "Trial USB session did not finish")
    require(run_until_promoted(session), "Confirmed trial left its record")
    healthy(session)
    return verify_single_slot(session, "slot_2", "slot_1", newer, newer)


def prove_rollback(state, old, new, older):
    session = forked(state)
    for attempt in range(1, TRIAL_LIMIT + 1):
        session = candidate_session(new, reuse=session)
        require(session.run_to_first_frame() == "condition met", "Trial image did not boot")
        require(trial_record(session) == (TRIAL_RUNNING, attempt), "Trial start was not counted")
    session = candidate_session(new, reuse=session)
    require(session.run_until_reset() == "system reset requested", "Trial was not rolled back")
    healthy(session)
    writes = verify_single_slot(session, "slot_1", "slot_2", older, older)
    require(trial_record(session) == (TRIAL_ROLLED_BACK, 0), "Rollback was not recorded")
    restored = candidate_session(old, reuse=session)
    notices = restored.machine.collect_notices()
    require(restored.run_until_waiting_for_key() == "reached board_wait_for_key",
            "Restored image did not report the rollback")
    healthy(restored)
    require(trial_record(restored) is None and notices, "Rollback report was not shown once")
    return writes


def prove(old, new):
    newer = (new / IDENTITY.update_file).read_bytes()
    older = (old / IDENTITY.update_file).read_bytes()
    require(older != newer, "Upgrade images are identical")
    session = install(old, newer)
    name = trial_install_name(old)
    trial = name is not None
    reached = session.machine.reached
    if trial:
        require(reached.get(name) == 1 and "updater_install" not in reached,
                "Expected exactly one trial installation")
    else:
        require(reached.get("updater_install") == 1, "Expected exactly one installation")
    result = {"ok": True, "mode": "trial" if trial else "both_slots", "readbacks": True,
              "headers_last": True}
    if trial:
        result["slot_writes"] = verify_single_slot(session, "slot_1", "slot_2", newer, older)
        require(trial_record(session) == (TRIAL_RUNNING, 0), "Trial record was not written")
        result["slot_2_kept"] = True
    else:
        result["slot_writes"] = verify_transaction(session, newer)
        result["slot_2_first"] = True
    if trial and has_trial(new):
        result["promotion_writes"] = prove_confirm(session, new, newer)
        result["rollback_writes"] = prove_rollback(session, old, new, older)
    installed = candidate_session(new, reuse=session)
    require(installed.run_to_first_frame() == "condition met", "New image did not boot")
    require(len(installed.panel.lit_sweeps()) == 1, "New image did not draw its first frame")
    healthy(installed)
    result["new_image_booted"] = True
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description="Prove an exact-image upgrade using each candidate's own ELFs; no player access")
    parser.add_argument("--old", type=Path, required=True, help="Frozen installed candidate directory")
    parser.add_argument("--new", type=Path, required=True, help="Frozen new candidate directory")
    args = parser.parse_args(argv)
    print(json.dumps(prove(args.old, args.new), sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit(str(error)) from None
