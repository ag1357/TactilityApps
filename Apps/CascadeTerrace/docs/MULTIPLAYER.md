# Anaphorum multiplayer initiation checkpoint

The 2026-10-07 initiation addendum authorizes building now on the CM5.
The separately untested P4 on-screen keyboard does **not** block this work.
Do not restart World SDK Gates I–VI, build Atlas, change firmware/networking,
or wait for production hardware, domains, accounts or an isolated NIC.

## Architecture and scope

One Python authority process owns one persistent world. Transport delivers
authenticated, bounded inputs/actions into `simulation.py`; simulation does
not own sockets. `storage.py` owns identities, sessions, SQLite archive and
receipts. `backend.py` wraps shared C collision/game rules through the
content-local `content/online_authority.c` projection. `inference.py` is a
server-only broker boundary. This is normal `build/cascade` integration,
not a renamed SDK viewer/fixture.

Independent account/player/device/session IDs are random 128-bit values.
World, scope and entity IDs accompany the versioned, little-endian protocol.
Inputs never select their actor or supply a position. Development enrollment
issues a random credential, stores only its verifier on the server, and
creates an owner-only client identity file. No MAC secret, browser, second
device or tailnet membership is required. An authentication-scheme field
leaves room for device public keys; provisioning/key-backed auth is not built.

Client movement uses shared collision prediction and acknowledgment/replay.
Only its main thread changes Game/render state. The worker owns networking
and a bounded mailbox. Nearby actor position/facing/phase are interpolated.
Snapshots are bounded full projections with scope, revision, public-state
SHA-256 and a checked own-state CRC. Corrupt own-state snapshots request
another full projection. Product/seed/world identity mismatch fails closed.
This does not claim client verification of the entire hidden public-world
digest: the client does not receive that complete world checkpoint.

Spatial cells select nearby peers, including vertical distance. A configurable
maximum of 32 peers bounds **each replica**, not total world population.
Identical public spatial queries reuse their nearest-peer list for one tick,
with self exclusion per caller. Cache lifetime ends on tick/join/leave/action.
Future private visibility policies must extend the cache key and filtering.
There is no all-player pose broadcast or fixed 128-player match boundary.
Logical world/scoped messages can host future partitions/instances; actual
multi-scope travel, partition ownership and divergent instances are not built.
The initial welcome role registry is Cascade-specific, not arbitrary content.

Consequential actions produce durable `(device_id, sequence)` receipts.
Changed retries and nonmonotone new sequences fail closed. One SQLite
WAL/FULL transaction stores the action, outcome, world/player checkpoint
and sequence. Failed commits restore both projections before propagating.
World/player snapshots are periodic (default 30 seconds), not per-frame
disk writes; disconnect also checkpoints personal state and frees its C
projection. SQLite files and migration destinations are owner-only.
Schema/product/seed mismatch requires explicit migration, never reset.

The client persists a finite owner-only action history before delivery,
retries until receipt and removes acknowledged entries. Its separate local
cache never overwrites the ordinary standalone save. Standalone offline play
remains unchanged. Disconnected enrolled play can journal **personal CONDENSE**
against retained budget. Reconciliation never trusts cached state, offline
clock progression, movement or newly claimed resources. Unsupported offline
operations are denied and budget exhaustion does not mint another reward.
This is an intentionally narrow vocabulary, not the completed offline mission.

## Run on the CM5

From `Apps/CascadeTerrace`:

```sh
make build/cascade build/libanaphorum.so
mkdir -p /chosen/private/anaphorum
python3 -m multiplayer.server --database /chosen/private/anaphorum/world.sqlite \
  --bind 127.0.0.1 --port 7788 --seed 42 --dev-enrolment \
  --metrics /chosen/private/anaphorum/metrics.json
```

In two terminals:

```sh
build/cascade --server 127.0.0.1 --port 7788 --identity /chosen/private/alice.keys
build/cascade --server 127.0.0.1 --port 7788 --identity /chosen/private/bob.keys
```

No server flag preserves ordinary offline mode. Do not use an ordinary save
as online authority. Online Save writes only the separate identity-associated
cache, and online Load/F9 cannot replace authoritative state.

Server bind/port/seed/storage, checkpoint/replication intervals, interest
radius/peer bound, session interval and inference endpoint are configurable.
The only implemented inference provider is `mock`; unknown providers refuse
startup. No model/S600 address is sent to clients, and no model can write
canonical state. Broker-to-NPC action integration remains below.

**Plaintext development transport only.** Loopback is the default. An explicit
`--allow-plaintext-lan` is required for LAN binding; do not expose it publicly.
The preserved home-LAN addresses are CM5 `192.168.1.171`, P4 `192.168.1.147`.
Nothing requires changing that network or joining public users to a tailnet.

## Tests and measured limits

```sh
make multiplayer-test
python3 scripts/qualify-multiplayer.py
bash scripts/sanitize-multiplayer.sh
python3 scripts/load-multiplayer.py --counts 64,128,256,512,1000 --seconds 4
```

Authority tests cover authentication, input bounds/sequences, receipt retry,
rollback, limited offline budget, schema permissions, SQLite backup migration,
projection release and spatial bounds/cache. A TCP fault proxy drives the real
normal C client through lost receipt/reconnect, bad CRC/full repair and
disconnected history/process-restart reconciliation. These scheduling faults
are tests, never fixture-ID branches in runtime code.

The two normal SDL clients use their actual input consumer, shared movement,
rendering, action routing and transport. Both move horizontally, see a peer,
reconcile and receive successful condense receipts. This is automated desktop
qualification, not human visual approval, P4 multiplayer or Internet testing.

`results/multiplayer/` preserves the dense-spawn TCP sweep and its earlier
failures. A 128-client unflow-controlled run hit the bounded input backlog.
The harness now keeps at most 32 unacknowledged inputs; this prevents claiming
capacity merely by dropping invalid input. The first 1,000-client full-loop
p99 was approximately **1,421 ms**, dominated by repeated dense interest
queries. One-tick query reuse reduced the measured full-loop p99 to **78 ms**.
The same CM5 run used about **66 MiB peak RSS** at 1,000 clients, with no
client failures and fanout bounded to 32.

The final repeated sweep's full-loop p99 was approximately 9/15/27/46/95 ms at
64/128/256/512/1,000 clients, with about 68 MiB peak RSS at 1,000. The first
post-cache sweep is also retained as `load-after-cache-first.json`.
These short colocated neutral-input runs include
startup/enrollment/teardown, not a sustained capacity SLA. More than 128
clients already crosses a 20 ms tick budget in this run, and the 1,000-client
clock/cadence is inadequate. Do not describe 1,000 connected clients as
qualified responsive gameplay. Diverse moving crowds and long-duration runs
remain necessary; identical-query reuse helps dense colocated queries only.

Reports include tick and full-loop p50/p95/p99, bytes/client/second, CPU time,
RSS, persistence/reconciliation samples, fanout and inference demand.
Load sweeps have zero consequential events/offline uploads/inference requests,
so their zero latency/demand fields mean **no samples**, not zero-cost work.
The separate normal-client/action and fault tests exercise persistence and
reconciliation. The current broker is not wired to NPC demand.

## Migration

Stop the authority cleanly or use the SQLite backup API, including WAL state.
Never copy only a live `.sqlite` file. For a live consistent backup, create a
new owner-only destination through `Store.backup()`; never overwrite an
existing destination. Migration tests reopen the backup and retain world ID,
player state and exact receipt results. Move client credentials/history/cache
privately, not into Git. On the final Pi 5, change bind/address/storage and
broker configuration; do not change identity meanings or world logic.
SSD, backups and an isolated S600 Ethernet interface are deployment concerns.

## Remaining mission, not deployment blockers

- Server-clocked WAIT/extraction/repair jobs. Legacy versions fast-forward
  shared time and are deliberately rejected online until adapted.
- NPC conversation/action integration through authorized broker views;
  proposal validation and canonical effects. Mutation-capable local dialogue
  is not run online. The current own-state projection still uses legacy State
  and needs a hidden-truth/private-NPC-state minimization audit.
- Full shared quest, player-player trade/PvP and destruction semantics.
  Existing bounded legacy actions are not the full multiplayer quest.
- Broader compact offline histories, conflict/epoch policy, fully independent
  offline sessions and full SDK multi-product/scope projection.
- Native P4 transport, device credential/history storage adapters, real
  CM5↔P4 latency/jitter/loss/bandwidth/heap/cleanup qualification.
- Sustained mixed loads, storage fault handling, flood controls, TLS,
  production provisioning/revocation and bounded DNS/shutdown behavior.

Preserve the current P4 normal artifact and 57-binding human profile. No
multiplayer package was deployed here. The remaining software-keyboard
visual/touch check is still open but does not hold multiplayer initiation.
