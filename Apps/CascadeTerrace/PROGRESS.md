# Recovery progress

## 2026-10-07, multiplayer initiation on CM5

- Continued published `5976d02` under the user's initiation addendum. The
  untested software keyboard stays separate; no final hardware, public domain,
  isolated NIC, account/browser or infrastructure detour was introduced.
- Added motion-only shared ticks and normal SDL client networking/prediction/
  acknowledgment replay/peer rendering. C worker never touches Game/LVGL.
  Existing offline tick/save behavior and SDK fixtures remain intact.
- Implemented one logical persistent authority: content-local C projection,
  typed stable identity/scope/entity messages, sequenced bounded inputs,
  nearby spatial replicas, durable SQLite action receipts/checkpoints/rollback,
  owner-only generated credentials and migration backup API. Content role/
  offline vocabulary lives in the backend, not socket/simulation logic.
- Persisted finite client action journals with retries, receipt removal and a
  separate offline cache; standalone user saves are not overwritten online.
  Only retained-authoritative-budget CONDENSE reconciles in this vocabulary.
  Legacy timed actions and mutation-capable enrolled dialogue remain blocked.
  Mock inference broker is independent and server-only, not yet NPC-integrated.
- Added real-client TCP fault tests: receipt dropped after commit, reconnect/
  exact replay, corrupt own-state CRC/full repair, disconnected journal,
  process restart and authoritative reconciliation. SQLite backup reopen
  preserves identity/player state/receipts; schema/product mismatch refuses
  startup instead of resetting data.
- Errors corrected before qualification:
  `storage size of 'tv' isn't known` (missing `<sys/time.h>`);
  `ModuleNotFoundError: 'multiplayer' is not a package` (test filename shadow,
  renamed `tests/test_multiplayer.py`);
  `conflicting types for 'process_event'; have 'void(const SDL_Event *)'`
  (matched forward declaration).
  An intentional proxy reset also emitted
  `ConnectionResetError: [Errno 104] Connection reset by peer` during teardown;
  the proxy now handles expected close errors and final tests rerun cleanly.
- Measured initial dense TCP load at 64 then 128. At 128, 48 synthetic clients
  hit the server's bounded input backlog. Preserved
  `load-128-backlog-failure.json`, then added realistic bounded acknowledgment
  flow control to the harness, not a larger server queue or fixture bypass.
- Flow-controlled sweep reached 64/128/256/512/1,000. Full-loop p99 at 1,000 was
  about 1,421 ms despite a much smaller physics-only tick metric. Reused
  identical public spatial queries for one tick with per-caller self exclusion,
  invalidation and a regression. First post-cache p99 was about 78 ms at 1,000.
  Preserved all pre-cache reports. Larger counts miss 20 ms and are not an
  acceptable sustained gameplay-capacity claim. Action/offline/inference cost
  is not represented by zero-work load fields.
- Commands completed: `scripts/qualify-world-sdk.sh`,
  `scripts/qualify-vip2.sh`, `scripts/sanitize-world-sdk.sh`,
  `scripts/sanitize-vip2.sh`, `make multiplayer-test`,
  `scripts/sanitize-multiplayer.sh`, `scripts/qualify-multiplayer.py`,
  real LVGL 9.3 conversation test and corrected-SDK P4 shared-core cross-build.
  Legacy 864/0, band parity 1,200/1,200, layout 95,861; 11 authority and three
  real-client fault tests; ten world/seven VI-P2 plus new authority/client
  ASan/UBSan checks. Existing ptrace limitation keeps leak detection disabled.
- New cross-built offline/shared-core ELF: 226,920 B,
  `a198b8435c9d541c0ba5a9b6615a8889f36aadb08216b1f229845e79c9847e53`,
  146 imports, none missing from pinned exports. Existing SDK dependency
  CMake warning and Kconfig notifications remain; no compiler warnings.
  This ELF was **not packaged/deployed** and does not include P4 transport.
  Installed `14ab8313…` UI artifact and human 57-binding profile stay untouched.
- Current plan/limits: `docs/MULTIPLAYER.md`, `HANDOFF.md`, `STATE.md`.
  Final source rerun passes; final load includes reader-failure propagation
  and all five requested counts with zero client failures. Final full-loop
  p99 is about 9/15/27/46/95 ms, peak RSS about 68 MiB at 1,000. Earlier
  post-cache figures remain in `load-after-cache-first.json`. Final code/test
  provenance and bounded claims are in `results/multiplayer/qualification.json`.
  Continue server-clocked jobs, NPC/private projection/full quest/offline
  semantics, then native P4 transport. Do not wait for production deployment.
- Publication failed twice with `remote: Internal Server Error` and
  `! [remote rejected] HEAD -> work/anaphorum-vipp4-frame-performance (Internal Server Error)`.
  Verified the remote still at `5976d02` after the first failure. Preserved the
  tested local commit and documented the normal retry, no force/reset/rebase.

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

### 2026-10-07 follow-up, physical wired input and manual Quit PASS

- Continued clean published `4c34501`. User reported: “controls work, new
  binding worked, cardkb worked on 3.3v.game quit”. Clarified that wired typing,
  Enter and repeat entry worked, and Back binding exited conversation.
  User explicitly did not test the on-screen keyboard.
- With the app closed, retrieved and privately backed up current controls,
  I2C config, full journal and latest normal session at
  `/media/cloud/2982-E16B/tactility_p4_work/backups/anaphorum-manual-20261007-5z6zstmq/`.
  No synthetic-probe summary in the manual session; 3,434 frames, 18 successful
  binding saves, final count 57. I2C mask 7 throughout 271 reports, final
  polls 35,950/failures 0; no input disconnects or drops.
- Manual Quit emitted input joined, close-save success and released buffers.
  Post-unload 31 tasks and PSRAM free 33,461,388 B. Input sampler mean
  14.132 ms, max 134.252 ms; do not claim a hard 10 ms sampling deadline.
- Verified the actual new 57-binding user profile after app restart and
  actual P4 reboot. Bounded opt-in probe supplied automatic Quit; each boot
  loaded 57 user bindings, all 16 synthetic checks passed, and the user file
  remained byte-identical to the new backup. Restored the normal package,
  verified installed ELF and probe-marker absence, left app closed.
- Generated `results/ui-recovery/manual-qualification.json` with bounded
  telemetry, user-confirmed scope and explicit remaining checks. No binding
  contents or entered text published. Never restore the older 47-binding
  baseline over the new human mappings.
- No code, firmware, renderer or network changes. Full UI promotion/multiplayer
  remains gated on keyboard-disconnected software entry/touch/visibility and
  keyboard absence in nonconversation modes, not on already-passed wired tests.
