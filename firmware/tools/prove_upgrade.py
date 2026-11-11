import argparse
import json
import struct
from pathlib import Path

import make_image
from identity import IDENTITY

CHUNK_SECTORS = 16
SLOT_SECTORS = 0x2800


def require(condition, message):
    if not condition:
        raise ValueError(message)


def healthy(session):
    require(not session.machine.faults and not session.machine.panics,
            f"Emulator failure: {session.machine.faults}, {session.machine.panics}")


def verify_transaction(session, newer):
    slots = {name: session.layout[name] for name in ("slot_1", "slot_2")}
    inside = {name: range(start, start + SLOT_SECTORS) for name, start in slots.items()}
    volume_start = session.layout["volume_start"]
    volume_end = volume_start + session.layout["volume_sectors"]
    writes = [item for item in session.card.writes if item[1] < volume_start]
    for _, lba, count in session.card.writes:
        require((volume_start <= lba and lba + count <= volume_end)
                or (count == CHUNK_SECTORS and any(lba in span and lba + count <= span.stop
                                                  for span in inside.values())), "Write outside permitted regions")
    for lba in session.changed_sectors():
        require(volume_start <= lba < volume_end or any(lba in span for span in inside.values()),
                "Changed sector outside permitted regions")
    by_slot = {name: [item for item in writes if item[1] in span] for name, span in inside.items()}
    require(all(by_slot.values()), "Both slots must be written")
    require(max(stamp for stamp, _, _ in by_slot["slot_2"])
            < min(stamp for stamp, _, _ in by_slot["slot_1"]), "Slot 2 must finish before slot 1")
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
    word = struct.unpack_from("<I", newer)[0]
    for name, start in slots.items():
        sequence = [(lba, count, value) for kind, lba, count, *rest in accesses
                    if kind == "W" and lba in inside[name] for value in rest]
        require(sequence[0] == (start, CHUNK_SECTORS, 0xFFFFFFFF), "Slot header must be invalidated first")
        require(sequence[-1] == (start, CHUNK_SECTORS, word), "Slot header must be committed last")
        require(start not in [lba for lba, _, _ in sequence[1:-1]], "Early header write")
        require(session.slot_image(name) == newer[:make_image.fw_end(make_image.FW_SIZE)],
                f"{name} does not match the exact new image")
    return len(writes)


def candidate_session(directory, files=(), reuse=None, historical=False):
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
                   files=files, reuse=reuse, vbus_after_first_frame=bool(files),
                   usb_overlay_elf=symbols / "echo_usb_overlay.elf",
                   module_elfs=sorted(symbols.glob("echo_module_*.elf")),
                   optional_hooks=("hal_system_set_usb_answer",) if historical else ())


def prove(old, new):
    newer = (new / IDENTITY.update_file).read_bytes()
    require((old / IDENTITY.update_file).read_bytes() != newer, "Upgrade images are identical")
    session = candidate_session(old, files=[(IDENTITY.update_file, newer, None)], historical=True)
    session.machine.note("updater_install")
    require(session.run_to_first_frame() == "condition met", "Old image did not boot")
    session.machine.stop_at("board_wait_for_power_hold")
    require(session.run_until_waiting_for_key(presses=1) == "reached board_wait_for_power_hold",
            "Old updater did not finish")
    healthy(session)
    require(session.machine.reached.get("updater_install") == 1, "Expected exactly one installation")
    require(session.machine.usb_connect_count >= 1, "USB transfer did not run")
    writes = verify_transaction(session, newer)
    installed = candidate_session(new, reuse=session)
    require(installed.run_to_first_frame() == "condition met", "New image did not boot")
    require(len(installed.panel.lit_sweeps()) == 1, "New image did not draw its first frame")
    healthy(installed)
    return {"ok": True, "slot_writes": writes, "slot_2_first": True, "headers_last": True,
            "readbacks": True, "new_image_booted": True}


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
