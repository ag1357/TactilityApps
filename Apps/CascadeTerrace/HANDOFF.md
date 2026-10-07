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

See `STATE.md`, `PROGRESS.md`, `results/ui-recovery/native-qualification.json`.
Final native runs pass 16 checks each, devices ready at `0x50`, `0x51`, `0x5f`,
zero I2C failures, successful save/join/buffer-release telemetry, 31→31 tasks.
User Controls remain byte-identical; three-device config survives reboot.
Real LVGL 9.3, full World/VI-P2 and ten + seven ASan/UBSan suites pass;
LVGL/VI-P2/seven sanitizers rerun after the wired-entry change.
Final corrected-SDK build: 226,864-byte ELF, 146 imports, none missing.
Installed normal package's downloaded ELF matches the preserved artifact.

## Not done

Synthetic events on real P4 are not physical touch/key injection. Keyboard
pixels, actual CardKB2 typing, pad buttons/axes/capture, software entry after
physical disconnect and visible reply/Leave need the brief hands-on check.
Do not promote the full physical UI gate from coordinate/protocol evidence.
No normal-game multiplayer, P4 transport or CM5↔P4 gameplay-link measurements.
Existing two-client SDK fixtures remain substrate, not normal-game integration.

## Current state

- Repo: `/media/cloud/2982-E16B/tactility_p4_work/repos/TactilityApps`.
- App: `Apps/CascadeTerrace`.
- Branch: `work/anaphorum-vipp4-frame-performance`, tracking `fork`.
- Previous published checkpoint: `22d9317ebe6942a282b006212fa5edfd5ffdd704`.
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
- `releases/ui-recovery/ui-probe.app` auto-quits after bounded synthetic tests.
  Do not deploy it for the hands-on test or mistake Home afterward for a
  background game. Do not reopen serial: doing so reset P4 previously.

## Exact next action

**Decision: obtain the remaining hands-on UI check on the already installed
normal candidate.** The human can launch Anaphorum once from Tactility Home,
test the short list below, then Menu → Quit. No network setup, reinstall,
binding restore or reboot-persistence exercise is needed.

## Needs from human

Visible conversation reply/input/Leave; actual wired typing and Enter;
disconnect keyboard and tap/submit/retap for software entry; no keyboard in
PLAY/Menu/Controls/capture; actual controls from both pads and Bind New;
Menu → Quit. Record what actually happened before promoting the gate.
Then retrieve the appended journal from
`/sdcard/tactility/user/app/ag1357.cascadeterrace/qualification.jsonl`.

## Do not repeat

No renderer optimization or Gates I–VI research. No resets to discard candidate
work. No Tactility/LVGL internal changes, firmware flashing, network changes or
Atlas. No multiplayer implementation until a physically verified UI recovery
checkpoint has been tested, committed and pushed. Then integrate two **normal
desktop game clients**, keeping movement responsive/predicted and persistent
actions authoritative through existing canonical events/receipts, before P4.
