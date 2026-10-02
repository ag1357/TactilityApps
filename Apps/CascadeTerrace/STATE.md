# Anaphorum recovery state, 2026-10-02

**Gate: BLOCKED on physical UI retest. Multiplayer has not started.**
This is a preserved candidate, not a promoted UI recovery checkpoint.

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
