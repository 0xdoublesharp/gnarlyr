# Phone companion Bluetooth data-link feasibility

## Decision

**No-go for companion implementation through the currently established module interface.** Settings, queue edits and mix transfer are not demonstrated. A data link might be feasible with the installed radio firmware, but neither an exact module identity nor a generic payload channel has been established. This is a decision to withhold implementation, not proof that the silicon cannot support BLE or SPP.

Spend at most **one further engineering day** on the identity/transport gate, within the unfinished-feature inventory's two-day investigation budget. Proceed only after arbitrary phone bytes reach the RKnano and an RKnano-generated response reaches the phone. Discovery, pairing, an audio link, a writable radio-local setting or an “OK” notification alone do not pass.

Source baseline: `a2d1a8b57726fc24a60280aa5843ba2b1c358715` on `main`. No player, phone, radio or other device was accessed for this study. No firmware feature code, build, CTest or emulator run was made; the coverage hold was not released. The existing Android scan files were read as historical host artifacts only.

## What the hardware and code actually establish

| Question | Established evidence | What remains unknown |
| --- | --- | --- |
| Chip/module identity | External Bluetooth subsystem with its own stack; GPIO2.7 power and UART1 control | Manufacturer, chip marking, module part/revision, firmware build and exact command manual |
| MCU interface | UART1 `0x400B0000`, GPIO2.16/2.17 group A, 115,200 baud, 8N1, FIFO, polled RX/TX | A supported raw-data mode, flow control, BLE service provisioning or RFCOMM channel selection |
| Audio transmission | SBC transmission to headphones is documented by FiiO and attributed to stock/Podium player observations in this tree | Exact SDP profile versions; concurrent audio and application data |
| AVRCP | Pause/play/previous/next semantics arrive as short module messages | Full AVRCP role/version/features; arbitrary vendor-dependent payload access |
| Audio reception from a phone | No sink-mode path established in this code | Whether installed radio firmware has an A2DP sink mode at all |
| BLE | Old phone files contain ambient advertisements, with no attributed ECHO connection | Connectable ECHO advertisements, GATT client/server role, services, characteristics, UART forwarding |
| SPP | No SPP API or generic send/receive command in the inspected source | An exposed SDP serial service and a usable RFCOMM-to-MCU bridge |

[Board Bluetooth](../firmware/platform/device/board/bluetooth.c) drives the module power and UART pins, configures the UART from the crystal with divisor 13, disables UART interrupts and modem control, and receives at most 256 bytes per service pass. RX overruns and peak/depth counters exist. [Protocol logic](../firmware/platform/device/logic/bt_module.c) has a 128-byte parsing buffer and a ten-byte outgoing command structure; [HAL Bluetooth](../firmware/src/hal/include/hal/bluetooth.h) exposes audio discovery, pairing, stream state and headset buttons, not generic data exchange.

This is a proprietary module-command protocol, **not a demonstrated Bluetooth HCI transport or a conventional `AT+...\r\n` parser**. Some frames contain binary bytes. The commands actually generated are:

| Bytes/operation | Current meaning |
| --- | --- |
| `CS00\0`, `CP00\0` | Start/stop device search |
| `CQ00\0` | Query link state |
| `CL06` + six raw address bytes; `CH06` + address | Connect, then retry |
| `CT00\0` | Disconnect |
| `43 59 30 1e 00` / `43 59 30 00 00` | Start/stop audio stream |
| `43 86 30 30 00` / `43 87 30 30 00` | Play/pause notices |

Receive handling recognizes `CV` ready/version, `CS` search results/end, `CQ` status/address/name, link results, and `CP`/`CC`/`CA`/`CB` headset actions. The ready handler saves only the fourth byte of its parsed frame as a version character. That is not a manufacturer/chip ID or a complete firmware version. Unknown incoming bytes are skipped, so the existing last-message UI cannot establish that no unrecognized data channel exists. An investigative build would need bounded raw UART capture before this parser.

SBC to headphones is consistent with an A2DP **source**; headset actions are consistent with the player side of AVRCP remote control. They do not establish an A2DP sink, SPP or BLE. Search results carrying an address and name are not evidence of BLE advertising; the stock audio workflow is compatible with Classic discovery. Main-MCU firmware cannot add a custom GATT service unless the radio's existing stack exposes a suitable configuration/data API or a usable controller interface.

## Stock and public-source evidence

[FiiO's ECHO specifications](https://www.fiio.com/echo_parameters) document SBC Bluetooth transmission, two CS43198 DACs and output jacks. They give no module part, BLE service contract or SPP interface. [FiiO's ECHO pairing instructions](https://www.fiio.com/newsinfo/1121794.html) describe searching for headphones and state that searching cannot continue while connected. This warrants a coexistence test; it does not prove all concurrent Classic/LE operation is impossible. The FAQ's codec answer itself mentions ECHO MINI, so do not use that wording to identify the full-size player's radio.

The applicant's F4402E/ECHO lab report, `MTi260331027-0101E1`, describes hardware V01/software 1.2.0, Bluetooth V5.1 and 79-channel radio testing ([original-report text hosted by device.report](https://device.report/m/e505e08250c27523cdb87664fa1fa75474f19976f5198c1a9c746fcf9257685e)). It does not identify the chip or application profiles. A Bluetooth version number, including conflicting third-party 5.3 claims, cannot establish an accessible GATT stack on the owner's unit.

The locally cached photograph from the [full-size F4402E teardown](https://www.reddit.com/r/snowsky/comments/1ri19zt/disassembly_of_fiio_snowsky_echo_f4402e_not_mini/) was inspected. It does not yield a defensible radio-chip/firmware identity. An antenna label is not the module part number; do not assign a chip from a similar Mini board. No authoritative full-size board schematic, radio BOM or matching vendor command manual was found in the inspected material.

The [stock reference](stock-firmware.md) says the saved analysis covers main, ADC module 1 and display module 4, not every one of the 91 stock load rows. [Ghidra preparation/export scripts](../ghidra/ExportEcho.java) export selected offline functions; they are not module firmware dumps. The cached verified main+ADC export's reset function at `0x030622ca` drives GPIO2.7, 2.16 and 2.17 low, consistent with board startup. It supplies no GATT/SPP implementation or chip identity. The inherited architecture's detailed Bluetooth command descriptions are useful attributed observations, not a newly exhaustive stock-stack audit.

Searches for the distinctive command strings did not identify a matching manufacturer's protocol. A superficially similar `AT+CS00` in [DFRobot's DFR0781 manual](https://wiki.dfrobot.com/dfr0781/docs/21721) disables a DAC, whereas this project's `CS00` starts discovery. That manual must not be used as this module's AT set, nor its BLE/SPP claims transferred to ECHO.

Historical `/Volumes/Scratch/SnowskyEcho/reports/bluetooth-feasibility-20261110/android-baseline.json` labels itself `advertisement_discovery_only`, `baseline-radio-uncontrolled`, with no attribution. `android-echo-bt-off-usb.json` is another scan-only observation. The scan permission used `neverForLocation`; [Android documents that this can filter some BLE beacons](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions). Neither artifact enumerates an ECHO GATT database or carries an MCU nonce. The existing scan-only APK is therefore insufficient for the acceptance probe below.

## Architecture if the gate passes

Prefer the player as a BLE GATT peripheral/server and the foreground Android app as central/client. This requires either an existing vendor serial service bridged to UART, or documented commands to configure a service. A fixed vendor service carrying arbitrary application frames is sufficient; a custom UUID is not essential. If the installed firmware only supports SPP, Android RFCOMM is a possible fallback after an actual service/channel and MCU payload bridge are demonstrated. Do not infer SPP from ordinary Bluetooth pairing. A reverse BLE role would also require documented module GATT-client operations; the current scan command does not provide them.

Use a bounded application protocol over the proven byte channel: version/capability negotiation, session nonce, request ID, length, operation, status, and transfer checksum. Reassemble fragments by negotiated MTU; acknowledge application completion separately from link writes. Serialize operations through existing settings/player/queue code and saved-state formats, using library fingerprints and stable track identities. Queue edits need an expected revision to reject stale changes. Preserve one shared operation layer for app, CLI and MCP clients.

Add explicit on-player approval for the intended phone and a way to revoke it before exposing mutations or file access. The feasibility nonce proves a path, not authorization. Select the project's authentication policy when designing that production access; the transport probe only changes a volatile scratch value. Keep file transfers in an allowlisted staging area with bounded size, capacity checks, chunk acknowledgments, resume offset and end-to-end hash; publish a complete mix only after validation. A transfer and USB mass storage must never own the same filesystem together. Pause/resume or reject transfers when USB storage takes ownership.

Separate a mix **manifest/playlist** from its **audio files**. A manifest referring to tracks already on the card may be only kilobytes. New audio is a slow bulk transfer: at the existing 115,200-baud 8N1 UART, the ideal unframed ceiling is 11,520 bytes/s, before protocol overhead, retries and audio coexistence. One MiB takes at least 91 s; ten MiB at least 910 s (15 min 10 s). This assumes every payload byte crosses this UART; another documented data bus could change the bound. Use USB for routine music imports unless actual goodput justifies a deliberately small wireless transfer. No current code demonstrates a second high-rate data path.

The board currently relies on frequent polling without RTS/CTS. A new channel needs its own bounded RX/TX queues and backpressure, service while rendering/reading storage, overrun accounting, and separation from `C...` control frames. The existing parser must not consume arbitrary binary payload as headset commands. Firmware format/overlay/RAM margins and radio connection count remain gates even if a phone can discover a service.

## Smallest decisive owner-run probe

The parent must arrange an explicit foreground Android/device window and obtain owner approval before changing radio/player state or installing any probe build. This study authorizes none of those actions. Use a reviewed recoverable image if instrumentation is needed. Store raw observations under `/Volumes/Scratch/SnowskyEcho/reports/feasibility/`; do not commit scans or phone identifiers.

### Stage A: discover and identify, without new feature firmware

1. On Android, open a foreground inspector/probe that can do BLE connect/service discovery/read/write/notify **and** Classic discovery/SDP. Grant the appropriate runtime Nearby Devices permissions; for a negative BLE result account for Android filtering/location policy. Verify the scanner against a known connectable peripheral. Record phone/OS, permissions, time and the exact player build.
2. Start with the player running normally, outside USB storage/DAC and charge-only loops, with headphones disconnected. Record a radio-off baseline, then the owner turns Bluetooth on and leaves its settings screen open so idle power-off cannot confound the scan. Repeat short bounded scans in ready and “Pair new device” states, then turn Bluetooth off again. Repeat the on/off correlation. Attribute candidates by controlled appearance/disappearance, proximity and service information, including unnamed candidates and changing addresses. A name alone is not attribution. The player's search for headphones need not make the player itself discoverable.
3. For an attributed connectable BLE candidate, connect and discover **all** services, characteristics, properties and descriptors. Record raw UUIDs and status codes; do not require advertised service UUIDs. Read only documented safe identity/version characteristics first. Identify an application-data characteristic from a matching vendor contract; standard battery/device-information services or a DFU service are not app transport. Do not write unknown controls/DFU characteristics.
4. For an attributed Classic candidate, discover SDP services and retrieve their UUID/channel information. If a Serial Port service is actually exposed, attempt the documented secure RFCOMM connection after the owner accepts pairing. A2DP/AVRCP UUIDs and a successful audio connection do not qualify. Android's [RFCOMM connection model](https://developer.android.com/develop/connectivity/bluetooth/connect-bluetooth-devices) requires matching service/client endpoints, which the current address-only audio connect command does not provide.
5. Save the complete observed radio identity/firmware strings. If no usable service appears, obtain the exact module part and installed-firmware command set through existing documentation/vendor information or documented identity queries. **No guessed AT commands, command sweeping, firmware updates or factory resets.** If identity and a supported payload mode cannot be resolved within the remaining day, stop with a no-go pending those named facts. A silent scan closes the bounded investigation, not the hardware possibility.

### Stage B: prove that bytes cross the module into the MCU

Stage A can pass discovery with no firmware change. The decisive test requires a documented UART-to-data mapping and a tiny, reviewed MCU echo harness; the current firmware has no generic echo operation. The parent should add only a volatile scratch counter/nonce responder plus bounded raw UART observation, with no settings writes, music import or persistent pairing changes beyond the chosen test phone. If the exact radio firmware offers no data mapping/configuration command, Stage B cannot be constructed by changing RKnano code alone.

1. Leave module firmware unchanged. Enable only the discovered/documented bridge mode or service. Capture UART bytes before the current `bt_module` parser, including unknown frames, with explicit overflow counters. Record the exact command bytes and vendor-manual revision that enable it; none is known from this study.
2. For GATT, subscribe to the documented response characteristic: enable local notifications and write the discovered CCCD (`0x2902`) with the correct notify/indicate value, waiting for completion. Read a safe test characteristic to obtain a **fresh MCU-generated** 16-byte boot nonce and counter. [Android's GATT data guide](https://developer.android.com/develop/connectivity/bluetooth/ble/transfer-ble-data) distinguishes service discovery, reads and notification registration; use their callbacks to record actual results.
3. Generate a fresh 16-byte phone nonce and write a request containing it and the MCU nonce using write-with-response. Observe the **same bytes in MCU RX**, not just an Android GATT success status. Have the MCU transform the phone nonce (for example XOR each byte with `0x5A`), increment its scratch counter, and send that result plus its boot nonce through the documented module transmit API. Require a phone notification containing that exact response. Read the characteristic again and require the updated counter/result. A radio-local echo with no MCU observation fails.
4. Repeat with binary payload containing `00`, `43`, `80` and `FF`, changing sequence numbers, at least one fragmented payload, and three disconnect/reconnect cycles. Reset only volatile test state between sessions. Verify no stale notification can satisfy the new nonce, and count parse errors, drops, duplicate requests, timeouts and UART overruns. For an SPP-only bridge, do the same exchange/readback over socket input/output; this proves SPP bidirectionality, **not GATT read/write/notify**.
5. Run a short control burst while wired playback runs, then while A2DP headphones stream, then with the screen off and after idle. A gate can pass with explicit “phone data and Bluetooth headphones are mutually exclusive” constraints if that product limitation is acceptable; report it rather than silently disabling audio. USB storage must reject/pause data mutations. Do not treat one working link as coexistence proof.

**Go for control:** both nonces and changed counter cross the phone → radio → RKnano → radio → phone path, GATT read/write/notify all succeed (or a clearly labeled SPP alternative), and reconnect/coexistence behavior is known. A proposed useful control target is 100 acknowledged 1 KiB transactions with zero incorrect responses/overruns and foreground p95 round-trip below 500 ms; record failures separately from successful latency.

**Go for audio-file transfer separately:** send a disposable 1 MiB file through the same MCU path, validate its hash after storage, interrupt/resume once without duplicate publication, and measure elapsed goodput alongside playback errors. A proposed minimum of 4 KiB/s makes that file take at most 256 s; below that, restrict the slice to settings, queues and manifests. This is a product target for the experiment, not established performance.

**No-go:** no attributable usable service, only audio/DFU/standard telemetry, no documented payload-to-UART bridge, phone writes never reaching MCU, no MCU response channel, or unacceptable coexistence/resource behavior. Missing evidence at the investigation deadline parks companion implementation without asserting permanent hardware impossibility. Report exactly which step stopped.

## Estimate and risks

Finish the remaining gate within **one engineering day plus a scheduled owner session**. The existing scan-only APK needs a connection/transaction-capable replacement or extension; a suitable foreground inspector can perform Stage A. Once a vendor bridge is identified, allow roughly **1–2 days** for a minimal harness and safe software checks if that exceeds the remaining gate budget; request a new scope decision before extending the investigation.

After a successful transport gate, estimate **5–10 engineering days** for one Android-first slice with acknowledged settings/queue edits and a small manifest transfer, consistent with the unfinished-feature inventory. New audio staging/resume adds roughly **3–5 days**, plus actual transfer/coexistence/recovery sessions. Cross-platform phone release, especially physical iPhone radio behavior, needs its own validation. These are planning estimates conditional on the radio API, not promises based on successful advertising.

The decisive risks are absent/locked vendor data APIs, unknown installed firmware, single-connection policies, audio-role restrictions, UART polling overruns, low bulk goodput, parser ambiguity and filesystem ownership during transfers. Replacing module firmware or hardware is outside this decision; without a supported interface there is no bounded estimate for that route.

## Evidence boundary

[Hardware](../firmware/docs/hardware.md), [architecture](../firmware/docs/architecture.md), [stock reference](stock-firmware.md), the linked implementation and manufacturer/Android sources support the distinctions above. The requested discovery/interface documents and `notes/unfinished-features.md` were read from the root checkout because they are absent here. The inventory's companion row requires a demonstrated bidirectional MCU link and rejects a negative name scan as a sufficient conclusion; its linked Bluetooth/design notes were not present at those referenced paths. No device result has been inferred from their absence, the simulator's fake devices, or the existing ambient advertisement files.
