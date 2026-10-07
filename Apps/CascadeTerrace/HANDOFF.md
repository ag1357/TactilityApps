# Anaphorum multiplayer initiation handoff, 2026-10-07

## Done

Continued published `5976d02` under the user's initiation addendum: build
multiplayer now on CM5, do not wait for final hardware/public networking or
the separately untested software keyboard. Implemented one persistent
authority and **normal SDL game** transport/prediction/reconciliation/peer
rendering, not a renamed fixture. Added stable account/player/device/session/
world IDs, bounded inputs and scope/entity actions, SQLite durable receipts/
rollback/checkpoints/backup migration, spatial interest and finite client
journals. Kept standalone offline saves independent. Added the server-only
mock inference abstraction without exposing model endpoints.

## Verified

Two normal clients move horizontally, see each other, reconcile and receive
successful personal condense receipts. TCP proxy tests prove lost-receipt
reconnect does not repeat the mutation, own-state CRC repair requests a full
snapshot, and disconnected bounded history survives a process restart and
reconciles against the server's retained budget.

CM5 short dense TCP measurements at 64/128/256/512/1,000 clients have bounded
32-peer replicas, no match/population cap and explicit flow control. Preserved
the earlier 128-client input-backlog failure and all pre-cache measurements.
One-tick identical-query reuse reduced the 1,000-client full-loop p99 from
about 1,421 to 78 ms. More than 128 clients exceeds the 20 ms budget in this
run; 1,000 connections are **not** qualified responsive gameplay.
Final repeated-sweep p99 at 1,000 was about 95 ms, peak RSS about 68 MiB.

Evidence and commands: `docs/MULTIPLAYER.md`, `results/multiplayer/`.
Preserved World SDK/VI-P2 qualifications and ten world/seven VI-P2 sanitizer
suites pass. New authority/C client ASan/UBSan and 11 authority + 3 real-client
fault tests pass; leak detection remains disabled for the existing ptrace
restriction. Shared-core corrected-SDK P4 cross-build passes, separately from
the installed UI artifact. No new multiplayer package was deployed.

## Not done

This is the first normal-game checkpoint, not completed multiplayer.
Server-clocked WAIT/extraction/repair and full shared quest are unfinished;
legacy fast-forward variants are rejected online. NPC broker/action routing
is not wired; only the mock boundary is tested. Own-state snapshots still use
legacy State and require hidden-truth/private-NPC minimization. Full offline
history vocabulary, player-player trade/PvP/destruction rules, multi-product/
scope traversal/partitioning and rare divergent instances remain.
No native P4 networking or CM5↔P4 gameplay-link measurements.
Transport is plaintext development-only, loopback by default, not public-ready.
Flood controls, production auth/TLS/revocation, storage-fault service behavior
and sustained mixed moving loads remain.

The user explicitly did not test the on-screen keyboard. Do not promote that
physical UI gate from coordinates/synthetic callbacks. Wired typing, Enter,
repeat, Bind New, Back, Quit and 57-binding reboot persistence already passed;
do not ask them to repeat those or block multiplayer on them.

## Current state

- Repo: `/media/cloud/2982-E16B/tactility_p4_work/repos/TactilityApps`.
- App: `Apps/CascadeTerrace`; branch `work/anaphorum-vipp4-frame-performance`,
  tracking `fork`. Resolve this handoff's containing checkpoint with
  `git rev-parse HEAD`. Prior published baseline was `5976d02`.
  **Local commit preserved, push blocked:** GitHub returned `Internal Server
  Error` twice. Remote was verified still at `5976d02` after the first failure.
  Do not reset this unpublished checkpoint.
- Server: `python3 -m multiplayer.server`; storage/bind/port/seed/checkpoint/
  interest/inference/session values configurable. `--dev-enrolment` allows
  automatic in-game development accounts. LAN plaintext requires explicit
  `--allow-plaintext-lan`; never expose it publicly. Broker provider: `mock`.
- Normal client: `build/cascade --server HOST --port PORT --identity PRIVATE_PATH`.
  The identity is owner-only `ANI3`, with `.history` bounded records and a
  separate `.cache`. No real credentials/database/user text are in Git.
- SQLite schema 2 checks schema/seed/product and refuses incompatible reopen.
  `Store.backup(new_destination)` uses SQLite backup, preserves identity and
  refuses overwriting an existing destination. Do not copy only a live DB file.
- CM5 `192.168.1.171`, P4 `192.168.1.147`, existing working home LAN.
  Do not switch to CM5's incompatible 5 GHz/channel-52 hotspot.
- Corrected SDK:
  `/media/cloud/2982-E16B/tactility_p4_work/repos/Tactility/release/TactilitySDK`.
  Do not use its stale nested 9.4 SDK; keep IDF 6.1.
- Installed P4 normal artifact remains
  `releases/ui-recovery/ag1357.cascadeterrace.app`; ELF SHA-256
  `14ab8313451aee7e84236ede72d735ea0209d79fed7259fc620ce6d85e44861c`.
  No auto-probe marker. App was closed before multiplayer work and no device
  launch/install occurred here.
- Current human backup:
  `/media/cloud/2982-E16B/tactility_p4_work/backups/anaphorum-manual-20261007-5z6zstmq/`.
  Preserve **57 bindings**, never restore the older 47-binding profile.
  Existing three-device I2C config is `0x50`/`0x51`/`0x5f`, SDA4/SCL5.

## Exact next action

Retry publication when GitHub accepts writes:

```sh
git -C /media/cloud/2982-E16B/tactility_p4_work/repos/TactilityApps push fork HEAD:work/anaphorum-vipp4-frame-performance
```

Then **implement server-clocked WAIT/extraction/repair jobs through the
content adapter and simulation queue, rather than calling legacy fast-forward
actions.** Preserve receipt idempotence/rollback and the responsive movement
lane. Qualify two normal clients against those jobs before full NPC/private
projection integration and P4 transport. Reuse `make multiplayer-test` and
the normal-client qualification; do not restart completed SDK gates.

## Needs from human

Nothing blocks the next implementation step. No new hardware, networking,
account/browser provisioning or repeated wired UI test is needed.
The brief software-keyboard visual/touch check remains optional parallel
human qualification, not a hold on the addendum's multiplayer work.

## Do not repeat

No firmware/LVGL/network changes, production infrastructure campaign, Atlas,
renderer rewrite or SDK Gates I–VI research restart. Keep private credentials,
databases, histories, bindings and human text out of Git/logs. Do not reopen
serial: it previously reset P4. Do not confuse probe Home, cross-build exports,
automated SDL inputs or 1,000 connections with physical/runtime/capacity proof.
