# Anaphorum UI recovery handoff, 2026-10-02

## Done

Recovered clean published `c5c9667` without resets; preserved local and device
data; fixed app-local focus/group isolation and bounded reply/input layout;
corrected an offscreen alignment regression found in the first physical round;
added Leave and production-helper LVGL regressions. FatFs, renderer, input/I2C
and explicit cleanup work are retained.

## Verified

See `STATE.md`, `PROGRESS.md` and `results/ui-recovery/` for exact evidence.
Portable layout and real LVGL 9.3 widgets pass. Gates I–VI and VI-P2 pass.
Corrected-SDK P4 build and import audit pass; package hashes/markers verified.
The human verified Bind New across an app restart only.

## Not done

The corrected candidate has **not** been installed or physically retested.
Keyboard visibility, reply/input visibility, Leave, reboot binding persistence
and final Quit cleanup still require hardware checks. Multiplayer has not
started: no normal-game network integration, native transport or CM5↔P4
measurements. The existing two-client fixture remains unchanged substrate.

## Current state

- Repo: `/media/cloud/2982-E16B/tactility_p4_work/repos/TactilityApps`.
- App: `Apps/CascadeTerrace`.
- Branch: `work/anaphorum-vipp4-frame-performance`, tracking `fork`.
- Published recovery reference: `c5c96674d26bd7a09333c52643130e91f5a6c233`.
  This handoff's containing commit preserves the blocked candidate.
- Corrected SDK:
  `/media/cloud/2982-E16B/tactility_p4_work/repos/Tactility/release/TactilitySDK`.
  The nested versioned SDK has stale 9.4 headers; do not use it.
- Candidate: `releases/ui-recovery/ag1357.cascadeterrace.app`,
  ELF SHA-256 `75708a0e0515680d74f8740f32ac7a11f836cffc9deab607399f60239dcd8b90`.
- P4 still has the failed first candidate (`e093572a...`) installed. The human
  reset it after Quit. Its former address was `192.168.4.185`.
- CM5 `ap0` disappeared. Human cannot restore device networking without
  dropping internet and explicitly requested a blocked checkpoint.
- Backup:
  `/media/cloud/2982-E16B/tactility_p4_work/anaphorum-recovery-20261002-L0lBPD/`.

## Exact next action

**Ask the human to provide a reachable P4 development-server address without
changing the CM5's working internet connection. Do not attempt a network fix.**

After access is supplied, ensure all existing Anaphorum instances are quit,
install the preserved corrected candidate once using multipart
`PUT /api/apps/install`, and launch once with
`POST /api/apps/run?id=ag1357.cascadeterrace`. Those launches stack instances;
do not repeat automatically. Do not use the old SDK or an older release package.

## Needs from human

A reachable P4 link, then hardware-present/absent conversation testing,
submit/retap, visible reply/input, Leave, PLAY/Menu/Controls/Bind New keyboard
absence, I2C controls, one saved mapping across app restart and P4 reboot, and
a clean Quit followed by telemetry retrieval.

## Do not repeat

No renderer optimization or Gates I–VI research. No resets to discard candidate
work. No Tactility/LVGL internal changes, firmware flashing, network changes or
Atlas. No multiplayer implementation until a physically verified UI recovery
checkpoint has been tested, committed and pushed. Then integrate two **normal
desktop game clients**, keeping movement responsive/predicted and persistent
actions authoritative through existing canonical events/receipts, before P4.
