# Anaphorum recovery state, 2026-10-07

**Native automated UI/lifecycle qualification: PASS. Physical touch/key and
display inspection gate: STILL OPEN. Multiplayer has not started.**
Device access is restored; this is no longer a network-blocked checkpoint.

## Current verified state

- Continued published `22d9317ebe6942a282b006212fa5edfd5ffdd704`, not a reset
  to an earlier research gate. CM5 and P4 use the existing home LAN:
  CM5 `192.168.1.171`, P4 `192.168.1.147`. CM5 remains the intended test server.
  Its current hotspot is 5 GHz/channel 52, incompatible with the P4's C6;
  no network configuration was changed to work around that.
- Preserved device saves, bindings, configuration and telemetry at
  `/media/cloud/2982-E16B/tactility_p4_work/backups/anaphorum-device-20261007-90dv0vk1/`.
  Opening passive serial reset the P4 despite DTR/RTS being disabled.
  This was not a Quit result. ModemManager was restored. Do not reopen serial.
- Added an explicitly packaged `ui-probe.flag` mode. After three rendered
  frames it exercises real app widget callbacks, requested/actual rectangle
  checks, focus/group isolation, submit queue, Leave and the Menu Quit button.
  It then follows normal cleanup. No NPC/canonical action is fabricated.
- Binding capture/save/overwrite/reload uses a separate
  `controls-ui-probe.cfg`, restoring the original input state afterward.
  The synthetic mapping survived app restart and an actual P4 reboot.
  Original `controls.cfg` remains byte-identical to the predeployment backup;
  each normal boot reloads its 47 bindings.
- The user confirmed two wired gamepads and CardKB2 on 3.3 V/GND/GPIO4/5,
  with gamepad 2's address trace cut. The app configuration previously listed
  only `0x50`; it now lists seesaw `0x50`, seesaw `0x51`, CardKB2 `0x5f`.
  Both seesaw product/read protocols and the keyboard FIFO responded:
  ready mask `7`, 15–16 polls and zero failures in the final short runs.
  The three-device configuration also survived reboot.
- A worker-published atomic readiness snapshot avoids reading mutable I2C
  state from the UI. The main task reports I2C status. A nonblocking journal
  mutex also serializes existing LVGL callback reports without a lock wait
  under LVGL; the final native runs report zero telemetry drops.
- Wired CardKB2 is an app-owned character adapter, not a kernel keyboard.
  Its confirmed readiness now keeps the textarea ungrouped/blurred, including
  intentional taps and arrival during software entry. Existing direct wired
  text routing remains unchanged. After adapter disconnect, the next tap
  uses ordinary focus. Host LVGL tests cover this policy and reconnect/retap.
- Actual P4 software-entry rectangles in the first automated round:
  input `[4,196,312,40]`, reply `[4,62,312,130]`, display height 480.
  The input ends at 236, above the expected keyboard edge at 240.
  Final wired rounds show hardware-ready layout without focused text or
  software-entry reservation. These are widget coordinates, not pixel captures.
- Final two runs passed 16 checks each, including all configured I2C devices.
  Both emitted `input_joined`, successful `close_save`, and `close_done`
  with buffers released. HTTP sysinfo returned from 31 tasks to 31 tasks;
  post-unload PSRAM free was 33,460,580 / 33,460,464 B. Internal heap varies
  with firmware services. These short runs are not long-duration leak tests.
- Installed the final **normal** package, downloaded/hash-checked its ELF,
  confirmed the probe marker was absent, and left the app **closed**.
  Automated probes quit to Tactility Home; no background game is left running.

## Current artifacts and checks

- Normal: `releases/ui-recovery/ag1357.cascadeterrace.app`, mode 0 plus
  qualification logging, **no automatic UI probe**.
- Probe: `releases/ui-recovery/ui-probe.app`, explicitly opt-in and auto-quits.
  Reproduce packaging with `python3 scripts/package-p4.py --mode 0 --qualification --ui-probe`.
  Do not repeatedly launch a normal app: the web API stacks instances.
- Final ELF: 226,864 B, SHA-256
  `14ab8313451aee7e84236ede72d735ea0209d79fed7259fc620ce6d85e44861c`.
- Normal package: 245,760 B, SHA-256
  `6fa87add1413d277ddc4d1a753817df69b25db87f7744b21ef1756315f5d66a2`.
- Probe package SHA-256:
  `40b0c85ad7783284c5995b7924ed068b3e2ef3d952204a1842f770947653fd58`.
- Corrected SDK remains `repos/Tactility/release/TactilitySDK` (LVGL 9.3),
  IDF 6.1, pinned firmware `d9f7434eb2cdceefb4aba0115a0c5116bb808712`.
  Native build passes; all 146 imports appear in pinned export tables.
  Actual execution/cleanup is recorded separately from the source-table audit.
- Full World SDK and VI-P2 qualifications passed, ten world and seven VI-P2
  ASan/UBSan suites passed. After the wired policy change, real LVGL 9.3,
  VI-P2 qualification and its seven sanitizer suites passed again.
  Layout: 95,861 checks; legacy: 864/0; band parity: 1,200/1,200.
  LeakSanitizer remains disabled by the existing ptrace restriction.
- Current evidence: `results/ui-recovery/native-qualification.json`,
  `gate.json`, `qualification-native.txt`, `wired-tests-native.txt`,
  `p4-build-native.txt`, `p4-imports-native.json`, `source-hashes-native.json`,
  `journal-guard-tests.txt`.
  The older unqualified physical failure is retained below and in Git history.

## Remaining mandatory gate

Synthetic events on the real P4 are **not** physical touch/key injection.
No keyboard pixels were inspected. The final UI still needs a short hands-on
check: visible reply/input and Leave, actual wired text/submit/retap, software
entry after disconnect, no keyboard in PLAY/Menu/Controls/capture, and actual
pad controls/capture. Protocol readiness is not a button/typing qualification.
CardKB2 specifies a 5 V power input; successful reads at the reported 3.3 V do
not establish reliable electrical operation. Keep P4 signal levels at 3.3 V.

After that evidence is recorded, TEST → COMMIT → PUSH the promoted UI checkpoint
before normal-game desktop multiplayer, then P4 transport and measured
CM5↔P4 latency/jitter/loss/bandwidth/memory. Keep the generic SDK boundaries,
bounded products/deltas, predicted movement and authoritative event receipts.
No Atlas, renderer campaign, firmware changes or completed-gate restart.

## Historical snapshot, 2026-10-02

The following records the earlier blocked recovery, **not current deployment
or access status**. It was published at `22d9317`; the current facts above
supersede its deployment, reboot-persistence and cleanup limitations.

## Recovery and scope

- Repo: `/media/cloud/2982-E16B/tactility_p4_work/repos/TactilityApps`.
- Branch: `work/anaphorum-vipp4-frame-performance`.
- Starting HEAD: `c5c96674d26bd7a09333c52643130e91f5a6c233`, clean, tracking
  `fork/work/anaphorum-vipp4-frame-performance`. Staged, unstaged, untracked
  and reference diffs were empty. No reset, restore or stash was used.
- Full app snapshot, ignored builds, status and binary patches:
  `/media/cloud/2982-E16B/tactility_p4_work/anaphorum-recovery-20261002-L0lBPD/`.
  App archive SHA-256:
  `8c999ad09fae335e6f5c00dd9236e2ee870425ba705e718bb7fb3c4ca91c8742`.
- Device saves, bindings, I2C config and pre-session telemetry were copied to
  that snapshot's `device-before/` before installing an app.
- All code changes are in this app. Tactility/LVGL internals, SDK world
  semantics, renderer, helper synchronization, input/I2C acquisition and
  cleanup ordering are unchanged. `core/action_input.c` retains the exact
  `c5c9667` FatFs direct-write fix.

## Candidate changes

- Remove the textarea from the default focus group outside intentional text
  entry. Explicit defocus events hide the firmware keyboard on leave.
- Conversation entry without a ready hardware keyboard does not focus text.
  A tap focuses it; submit/retap and hardware-unplug/retap are covered by the
  host widget regression. Ready hotplug keyboard devices are detected through
  the public ledger; the firmware still decides whether to show its keyboard.
  Deprecated always-ready keyboard presence APIs are not exported or used.
- An app-owned, paged reply label and input share a bounded layout above the
  firmware's bottom-half keyboard. No keyboard objects are inspected.
- Explicit top-left placement clears prior bottom-center alignment. A visible
  Leave button exits conversation. Reply text can be paged by tapping it.
- Telemetry records both requested and actual LVGL input/reply rectangles.
- Add portable layout, real-LVGL widget and binding-overwrite regressions.
  The real-LVGL test uses production app helpers, with simulated firmware
  keyboard hooks. It is not a P4/Tactility/input-device execution claim.

## Physical round: FAILED, not superseded by host tests

The first candidate (`e093572a...`, 222,168-byte ELF) was installed and
launched once at `192.168.4.185`, using multipart install, after the human
quit previous instances. The human reported:

- Bind New worked and survived app restart.
- Conversation input was not visible; software entry could not be exercised
  after disconnecting CardKB2.
- No direct conversation exit was apparent except Menu.
- Quit was requested, but full cleanup was uncertain; the human reset P4.
- Binding persistence after device reboot was **not checked**.

The app-side cause found afterward: the textarea's old bottom-center
alignment remained active while new root-local offsets were applied, placing
it offscreen. The current candidate fixes this and adds Leave.
**These corrections have not been installed or physically retested.**
The P4 still has the failed first candidate installed.

After reset, CM5 interface `ap0` disappeared and the old P4 address became
unreachable. Telemetry retrieval failed with a connection timeout, so no
fresh close/heap or actual-coordinate evidence is claimed. The human cannot
restore the hotspot or join P4 to the current internet network without
interrupting internet, and explicitly requested offline checks plus a
blocked checkpoint. Do not alter CM5 networking or firmware.

## Build provenance and evidence

Use the corrected SDK at:

```text
/media/cloud/2982-E16B/tactility_p4_work/repos/Tactility/release/TactilitySDK
```

Do **not** use the sibling `0.8.0-dev-esp32p4/TactilitySDK` path: its LVGL
headers are 9.4.0 and its configuration snapshot is missing. An initial build
against that path was rejected and never installed. The corrected path
contains LVGL 9.3.0, propagates `LV_CONF_KCONFIG_EXTERNAL_INCLUDE`, and its
LVGL archive matches the pinned firmware build byte-for-byte. Firmware
checkout remains clean at `d9f7434eb2cdceefb4aba0115a0c5116bb808712`.

- ESP-IDF 6.1.0 and existing `espressif-idf6` tools.
- Corrected candidate: 222,784-byte ELF, 235,520-byte mode-0,
  qualification-marked package; 144 imports, none missing from pinned
  firmware export tables. Source-table membership is not a loader claim.
- ELF SHA-256:
  `75708a0e0515680d74f8740f32ac7a11f836cffc9deab607399f60239dcd8b90`.
- `.text` 87,846 B, `.rodata` 114,348 B, `.bss` 147,864 B.
  These are ELF sections, not measured live internal RAM/PSRAM consumption.
- No new physical RAM, latency, renderer, link or reconnect measurements.
- Build still reports the SDK component-dependency CMake warning and
  pre-existing Kconfig notifications. No compiler warnings or LVGL
  configuration pragma in the corrected build.

Evidence: `results/ui-recovery/`, `results/vip2/`, `results/worldsdk/`,
`results/p4-build.json`. Candidate package:
`releases/ui-recovery/ag1357.cascadeterrace.app`.

## Offline checks

- Portable conversation layout: 95,861 cases pass.
- Real LVGL 9.3: actual input/reply coordinates, intentional focus,
  ungrouped menus, submit/retap, unplug/retap and leave blur pass.
- Legacy: 864 checks, no failures, save 734 B, wire CRC32 3349299003.
- Gates I–VI qualification and parity pass, including 18/18 traversal,
  15/15 adversarial, the existing 2,385-check network fixture, social
  parity and two rendered SDK clients. These are not normal-game multiplayer.
- VI-P2 qualification passes, including raster band parity 1,200/1,200,
  lifecycle 74 checks and both I2C suites.
- Final sanitizer rerun passes: ten world and seven VI-P2 ASan/UBSan suites.
  LeakSanitizer is disabled under the existing workspace ptrace restriction.
  The real-LVGL host regression is a separate nonsanitized test.
- P4 build, import audit, package hash/content verification and
  `git diff --check` pass.

Reproduce the new widget check without downloading or modifying LVGL:

```sh
make conversation-lvgl-test \
  LVGL_SOURCE=/media/cloud/2982-E16B/tactility_p4_work/repos/Tactility/managed_components/lvgl__lvgl
```

## Gate order remains mandatory

1. Restore a P4 access path without dropping internet.
2. Install the corrected candidate once; physically verify keyboard-present
   and keyboard-absent entry, visible reply/input, submit/retap, Leave, menus,
   Controls/capture, I2C input, Bind New across app restart **and reboot**,
   and Quit/helper joins/heap return.
3. Test, commit and push the physically qualified UI checkpoint.
4. Only then integrate multiplayer into two normal desktop game clients.
   Existing SDK fixtures are substrate, not that gate.
5. Only after desktop passes, port the same semantics to P4 and measure the
   real CM5↔P4 link before choosing cadence.

Keep world/product/recipe identity, scope/region/entity IDs, authoritative
revisions/hashes and bounded products/deltas generic. Separate predicted
movement/presentation from consequential authoritative events and receipts.
No Atlas, new renderer campaign, content, combat or firmware work.
