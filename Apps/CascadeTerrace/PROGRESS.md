# Recovery progress

## 2026-10-02, UI recovery, BLOCKED

- Recorded clean `c5c96674d26bd7a09333c52643130e91f5a6c233` HEAD/status,
  empty staged/unstaged/reference diffs, and the app snapshot described in
  `STATE.md`. Read FACTORY_CONTINUATION, WORLD_SDK_ARCHITECTURE,
  WORLD_SDK_MAPPING, VI_P2_REPORT and the published reconciliation evidence.
  No completed research gates restarted.
- Preserved device app data before install. Implemented app-only focus and
  conversation layout changes; retained FatFs persistence and renderer/input
  work. `make conversation-layout-test action-input-test test` passes.
- The first portable layout sweep exposed two tiny-window cases. Failure:
  `conversation_layout_test: tests/conversation_layout.c:18: main: Assertion 'c.reply.h>0&&c.reply.h<=140' failed.`
  Fixed the minimum-space check and split before proceeding; 95,861 cases pass.
- Ran `scripts/qualify-world-sdk.sh`, `scripts/qualify-vip2.sh`,
  `scripts/sanitize-world-sdk.sh`, `scripts/sanitize-vip2.sh`. Initial run:
  all qualifications, ten world and seven VI-P2 ASan/UBSan suites pass.
  LeakSanitizer remains disabled as documented by the existing scripts.
- Initial P4 build/import audit passed syntactically but revealed the stale
  SDK's LVGL configuration pragma and 9.4 headers. Rejected that artifact.
  Rebuilt against the existing corrected 9.3 SDK; no firmware/SDK files changed.
- Wrong octet-stream install attempt failed:
  `urllib.error.URLError: <urlopen error [Errno 104] Connection reset by peer>`.
  Checked the endpoint's required multipart form, then multipart install
  returned `200 ok`; exactly one launch returned `200 ok`.
- First physical UI round failed. Human confirmed app-restart binding
  persistence, but input was invisible, software entry could not be tested,
  direct leave was missing and full Quit cleanup was uncertain. Human reset
  the P4; reboot binding persistence remains unchecked.
- Found the retained LVGL bottom-center alignment; corrected production
  placement to TOP_LEFT, added Leave, actual-coordinate telemetry and the
  real LVGL regression using the same production widget helpers.
- Device telemetry fetch failed after reset:
  `urllib.error.URLError: <urlopen error timed out>`.
  Host network check:
  `Cannot find device "ap0"`.
  No network configuration or services were changed. Human explicitly chose
  “No, finish offline checks and preserve a blocked checkpoint.”
- `make conversation-lvgl-test LVGL_SOURCE=/media/cloud/2982-E16B/tactility_p4_work/repos/Tactility/managed_components/lvgl__lvgl`
  passes (real LVGL 9.3, firmware hooks simulated). It verifies actual widget
  coordinates, not only the portable layout calculation.
- Repeated all qualification/sanitizer commands after the final source
  changes. Final rerun completion is appended below.
- Final corrected-SDK P4 build and import audit pass: ELF 222,784 B,
  144 imports, zero missing. Packaged mode 0 + qualification flag;
  verified package and embedded ELF hashes. Recorded source hashes and ELF
  sections in `results/ui-recovery/`.
- Corrected candidate is not deployed. No new physical results or multiplayer
  integration are claimed. Preserve and publish this blocked checkpoint,
  then stop for device access and hands-on retest.

### Final offline rerun complete

The final chained run completed successfully: Gates I–VI and VI-P2
qualification, ten world and seven VI-P2 ASan/UBSan suites. No suite failed.
The real-LVGL test is separate and nonsanitized. Full final command output is
preserved at `results/ui-recovery/qualification.txt`, `p4-build.txt` and
`lvgl-build.txt`. P4 compiler warnings: none; existing SDK CMake warning and
Kconfig notifications remain as recorded in `STATE.md`. Candidate package
hash/content verification and `git diff --check` pass.

## 2026-10-07, native UI qualification, hardware gate partially verified

- Continued clean published `22d9317`. Found P4 `192.168.1.147` on the
  working home LAN, CM5 `192.168.1.171`. Existing AP uses 5 GHz/channel 52;
  retained LAN instead of disrupting internet. Preserved device data in the
  backup referenced by `STATE.md`.
- Deployed/hash-verified the corrected `75708a0e…` ELF once. A large HTTP
  download during rendering timed out. Passive serial opening reset P4 even
  with DTR/RTS disabled; recorded as reset, not Quit. Restored ModemManager.
- Added the opt-in packaged UI probe, separate persisted test-bindings file
  and actual widget/event checks. Initial automated round passed, then 15
  checks each passed on app restart and actual device reboot. Original
  controls remained byte-identical. Tasks returned from 31 to 31 after Quit.
  Software-entry input `[4,196,312,40]` ended above the keyboard half-screen.
- Commands passed:
  `make conversation-layout-test action-input-test test`,
  `make conversation-lvgl-test LVGL_SOURCE=/media/cloud/2982-E16B/tactility_p4_work/repos/Tactility/managed_components/lvgl__lvgl`,
  `scripts/qualify-world-sdk.sh`, `scripts/qualify-vip2.sh`,
  `scripts/sanitize-world-sdk.sh`, `scripts/sanitize-vip2.sh`.
  Ten world and seven VI-P2 sanitizer suites clean; LSan remains disabled.
- First normal-install verification used the wrong `/sdcard/tactility/data/app`
  path: `KeyError: 'entries'`, response `{"error":"scan failed"}`.
  Installation itself succeeded. Read-only listing found the correct
  `/sdcard/tactility/app` path; verified the installed ELF and marker absence.
  No repeated launch occurred.
- User confirmed physical 3.3 V/GND/GPIO4/5 wiring, keyboard and two pads,
  with a trace cut on pad 2. Device config previously listed only `0x50`.
  Preserved that file, installed the three-entry app config, and added atomic
  worker readiness snapshots with main-thread-only telemetry. Actual native
  protocol responses confirmed `0x50`, `0x51`, `0x5f`, mask 7, zero failures.
- Corrected app-owned wired text entry: CardKB2 isn't in the kernel keyboard
  ledger. Its published readiness now blurs/ungroups the textarea rather than
  triggering the firmware keyboard. Existing direct I2C text/Enter/Escape
  routing is retained. Added production-helper LVGL wired-entry/disconnect test.
- Repeated real LVGL, VI-P2 qualification, seven VI-P2 ASan/UBSan suites and
  corrected-SDK native build/import audit after that final change. All pass.
  Final native restart and P4-reboot rounds pass 16 checks each, all three
  devices ready, input ungrouped/blurred for wired entry, explicit Quit cleanup,
  31→31 tasks, successful saves and post-unload PSRAM approximately 33.46 MB.
- Final review found that existing LVGL callbacks can also report layout while
  main-thread telemetry closes/reopens the journal. Added a qualification-only
  nonblocking journal mutex and dropped-report counter, avoiding a wait under
  LVGL. Rebuilt/audited and repeated the focused host checks plus two native
  restart/reboot runs: all 16 checks present, all devices ready, zero telemetry
  drops, successful Quit, user Controls intact. No steady-state input locks,
  firmware or renderer synchronization were changed.
- Final ELF `14ab8313…`, 226,864 B; 146 imports, zero missing. Normal and
  auto-quitting probe artifacts preserved separately. Installed/hash-checked
  normal package, verified no probe marker and retained three-device config;
  app left closed with original user bindings untouched.
- Evidence explicitly labels synthetic events and uninspected keyboard pixels.
  No physical typing/buttons/touch claim or multiplayer implementation.
  Current checkpoint remains gated on that small hands-on UI check.
