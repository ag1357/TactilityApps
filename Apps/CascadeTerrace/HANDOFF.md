# Anaphorum native UI qualification handoff, 2026-10-07

## Done

Continued published `22d9317`, restored access over the existing home LAN,
preserved device data, deployed the corrected UI and added an explicitly
opt-in native UI probe. Verified actual widget placement, focus/group isolation,
submit callback, Leave, persisted synthetic Bind New and normal Quit cleanup
across app restart and device reboot. Confirmed both physical gamepads and
CardKB2 on GPIO4/5. Added wired-entry focus policy without firmware internals.
FatFs overwrite, renderer/helper synchronization and input acquisition remain.

## Verified

The follow-up human test confirmed controls, Bind New, wired CardKB typing,
Enter/repeat entry, Back-binding conversation exit and manual Quit. Retrieved
the normal session: 3,434 frames, 18 successful saves, all three I2C devices
ready, 35,950 polls/zero failures, no input disconnects/drops, clean save/join/
buffer release and 31 tasks after unload. Current **57 user bindings** were
preserved and reloaded byte-identically after app restart and actual reboot,
using the bounded synthetic probe only for automated Quit. See
`results/ui-recovery/manual-qualification.json`.

See `STATE.md`, `PROGRESS.md`, `results/ui-recovery/native-qualification.json`.
Final native runs pass 16 checks each, devices ready at `0x50`, `0x51`, `0x5f`,
zero I2C failures, successful save/join/buffer-release telemetry, 31→31 tasks.
User Controls were unchanged by those automated tests; the user's later
57-binding profile supersedes the original 47-binding backup. Three-device
config survives reboot.
Real LVGL 9.3, full World/VI-P2 and ten + seven ASan/UBSan suites pass;
LVGL/VI-P2/seven sanitizers rerun after the wired-entry change.
Final corrected-SDK build: 226,864-byte ELF, 146 imports, none missing.
Installed normal package's downloaded ELF matches the preserved artifact.

## Not done

The user explicitly did not test the on-screen keyboard. Software entry/touch
after disconnect, visible reply/input/Leave, and keyboard absence outside
conversation still need the brief hands-on check. Synthetic events on real P4
are not physical touch/key injection. Wired typing/control/capture/Back/Quit
already passed; do not ask the user to repeat those tests.
Do not promote the full physical UI gate from coordinate/protocol evidence.
No normal-game multiplayer, P4 transport or CM5↔P4 gameplay-link measurements.
Existing two-client SDK fixtures remain substrate, not normal-game integration.

## Current state

- Repo: `/media/cloud/2982-E16B/tactility_p4_work/repos/TactilityApps`.
- App: `Apps/CascadeTerrace`.
- Branch: `work/anaphorum-vipp4-frame-performance`, tracking `fork`.
- Previous published checkpoint: `4c34501b2e89b30a3c7a964b6e36bdccb906dd20`.
  Resolve this handoff's containing commit with `git rev-parse HEAD`.
- Corrected SDK:
  `/media/cloud/2982-E16B/tactility_p4_work/repos/Tactility/release/TactilitySDK`.
  The nested versioned SDK has stale 9.4 headers; do not use it.
- Installed normal candidate: `releases/ui-recovery/ag1357.cascadeterrace.app`,
  ELF SHA-256 `14ab8313451aee7e84236ede72d735ea0209d79fed7259fc620ce6d85e44861c`.
  Mode 0, qualification logging, no auto-probe marker. **App is closed.**
- P4 `192.168.1.147`, CM5 `192.168.1.171`, working home LAN. CM5 is the
  intended test server. Do not switch to its incompatible 5 GHz hotspot.
- Saved app I2C config selects `i2c-external`: two seesaws `0x50`/`0x51`,
  CardKB2 `0x5f`. SDA4/SCL5, firmware bus 100 kHz. Power reliability at reported
  3.3 V is not proven; CardKB2 documents 5 V input, P4 signals must stay 3.3 V.
- Current backup:
  `/media/cloud/2982-E16B/tactility_p4_work/backups/anaphorum-device-20261007-90dv0vk1/`.
  Original recovery archive is still at the path in `STATE.md`.
- Latest human binding/telemetry backup:
  `/media/cloud/2982-E16B/tactility_p4_work/backups/anaphorum-manual-20261007-5z6zstmq/`.
  Preserve these **57 bindings**. Never restore the earlier 47-binding profile.
- `releases/ui-recovery/ui-probe.app` auto-quits after bounded synthetic tests.
  Do not deploy it for the hands-on test or mistake Home afterward for a
  background game. Do not reopen serial: doing so reset P4 previously.

## Exact next action

**Decision: obtain only the keyboard-disconnected visual/touch check on the
already installed normal candidate.** No network setup, reinstall, binding
restore or repeated wired-controls/reboot-persistence test is needed.

## Needs from human

Disconnect the keyboard, launch Anaphorum once from Home, confirm visible
reply/input/Leave, tap/type/submit/retap using the on-screen keyboard, and
check that no keyboard appears in PLAY/Menu/Controls/Bind New. Then Quit.
Record what actually happened before promoting the gate.
Then retrieve the appended journal from
`/sdcard/tactility/user/app/ag1357.cascadeterrace/qualification.jsonl`.

## Do not repeat

No renderer optimization or Gates I–VI research. No resets to discard candidate
work. No Tactility/LVGL internal changes, firmware flashing, network changes or
Atlas. No multiplayer implementation until a physically verified UI recovery
checkpoint has been tested, committed and pushed. Then integrate two **normal
desktop game clients**, keeping movement responsive/predicted and persistent
actions authoritative through existing canonical events/receipts, before P4.
