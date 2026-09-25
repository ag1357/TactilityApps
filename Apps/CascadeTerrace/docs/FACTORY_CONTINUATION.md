> VI-P2 continuation: [report](VI_P2_REPORT.md). This proving branch preserves the long mission and Gates I–VI. Input/layout/lifecycle implementation and host/P4 build gates pass; physical and shared-input ownership qualifications remain. Do not advance to multiplayer from this checkpoint.

# Factory continuation: World SDK foundation

Repository: ag1357/TactilityApps. Branch: `work/cascade-world-sdk`.
Starting commit: `f216216d4b2f407a7684dcfb455833b116a7de1b`.
This handoff describes its containing checkpoint. Resolve the exact current
checkpoint with `git rev-parse HEAD` after fetching this branch; do not start
again from the cognition branch. No unpublished context is required.

**Status: SIXTH IMPLEMENTATION GATE PASSED.** The SDK continuous-traversal
promotion gate passes 18/18 declared walk edges, the adversarial spatial
suite passes 15/15 with impossible connections failing generation cleanly,
and the macro-geography gate passes: the full §49 hierarchy is generated
from one seed as a 1,938-byte schema-3 product whose wilderness route is
derived from the generated river (not hand-positioned) and walkable end to
end, with the terrain cost model proving the route-cost case (nearest
settlement is not the cheapest destination). The resource/ecology gate
passes: four geography-derived reservoirs, kind-aware extraction with
sparse persistent sites, rate-based recovery with deterministic weather
and pit healing, visible water drawdown on the existing river records,
and the conservation ledger identity held across chunks, compaction and
the wire. The nonlocal Phos topology gate passes: one deterministic
Phos-rich plane over the karst lode joins the ordinary forest ruin through
a typed anomaly edge (link kind 3, anchor reservoir index in the reserved
byte) whose activation is a pure function of the canonical karst Phos
state (open iff 2×level ≥ capacity), with a separate 1,500 toll under
CAP_ANOMALY, instant state-gated crossing, ordinary geography and cost
literals preserved bit-for-bit while closed, live removal on depletion
with recharge-driven reopening, server authority over use, client-side
derivation from the public replica, reconnect and restarted-server
restoration of the closed gate, and fail-closed malformed rejection on
both the C and Python sides. The creature/NPC gate passes: eight
geography-derived species (fauna across the four biomes plus the required
gate/haven wardens and their NPC residents) ride the macro product as
schema 4 with stable ancestry-derived identities, placement is solved from
the product alone (anchored slots on the anchor walk surface, fauna inside
their biome reservoir on the terrain top, never blocked), schedules are
pure functions of (identity, now) resolved through the access graph with
route-cost-weighted dwell/travel cycles, window queries materialize sparse
offscreen entities without tick replay, and persistent death/relocation/
pinning exceptions override the generated defaults through the authority
with fail-closed codes — all with bit-exact C/Python parity over every
creature and time, including 32-bit clock wrap, and Gate I–IV literals
preserved bit-for-bit. The canonical event/social gate passes: the
existing game actions cross one canonical boundary
(`content/cascade_adapter.c` bridge with a bounded honest vocabulary,
legacy gameplay authority and outcomes proven byte-identical) into
versioned events with retry receipts, witness projection onto actual NPCs
through the engine's own geometry (occlusion and range literals exact),
directed evidence capsules with hearsay degradation and time decay,
separate conduct/relationship projections, delayed institutional dispatch
on the canonical clock, hidden-truth isolation across three world
variants, and the full wire-v4/save/compaction round trip — with
C/Python parity over every observer-subject pair at every step, and the
two-client TCP scenario proving private speech, receipt replay after
reconnect and the delayed guild notice through a restarted server. The
mapping sheet the content schema requires is published as
`docs/WORLD_SDK_MAPPING.md`. The normal Cascade game
remains playable and passes its original suite. The bounded renderer
evaluation is complete and the hybrid renderer it recommended is
integrated and promoted (incremental-span rasterizer + portable exact-2x
presentation; PIE and PPA gated experimental pending physical P4
qualification). The complete multiplayer quest and all physical P4
claims remain pending. The two-client SDK fixture is not the full
Kyra/intake multiplayer game.

## Published checkpoints

| Milestone | Commit |
|---|---|
| A: compiled schema skeleton | 9085bd8625cfd24025fc2dfeda7faff43b4163a9 |
| B: binary recipe compiler | a9bc5b64f2786f4f9b3865221dca39edebc4c983 |
| C: deterministic products, adversarial validation | 07271d1a60618bd5716ea45c6500c8de71d8440a |
| D: legacy site adapter and replay | e045cde769c7e7b73229e4f016be68a92fa2f33b |
| E: shared renderer/collision, lift/room fixture | e88b93feeee94dbc0ad1c2ee2570f6fdc3baf842 |
| F: semantic authority/TCP protocol fixture | 987195d8763e99cd29906593a0a2a13b2a7168ee |
| G: checkpoint recovery and compaction | ef5cbc0cb22f3c691646dbae7518871e8a9ca5ed |
| H: rendered multiplayer foundation, qualification and this handoff | containing commit; full game integration remains incomplete |
| I: spatial/access repair, 18/18 traversal, adversarial suite | `2886c89` |
| II: macro geography, sparse rivers, derived route, terrain costs | `5d35f82` |
| III: regional reservoirs, extraction sites, ecology recovery, ledger | `9862e06` |
| IV: nonlocal Phos anomaly topology, state-gated traversal, winze lift | `7b85d38` |
| R0: renderer evaluation (bounded, pre-rewrite) | `ca942d3` |
| RA: save-header regression fix | `e835061` |
| RB: incremental-span rasterizer, exact tested depth/coverage (promoted) | `8bbe12e` |
| RC: exact portable 2x presentation + gated PIE/PPA backends | `b1229f0` |
| RD: presentation lifecycle reconciliation (open-once at init) | containing commit |
| V: sparse deterministic creatures and schedules | containing commit |
| VI: canonical events, witnesses and social projections; mapping sheet | `f47a60d` |
| VI-P: first physical P4 round on Device A (display fit, measurements, durable telemetry) | containing commit |

## Build and reproduce

All commands below run from `Apps/CascadeTerrace`.

```sh
make test
python3 tools/worldsdk/sdk.py schema
./scripts/qualify-world-sdk.sh
```

The qualification script now exits **0** with the traversal, adversarial,
macro-geography, resource/ecology, nonlocal anomaly, creature and
canonical event/social gates green. Do not
change it to ignore a failure if one reappears; it must exit nonzero when
any gate fails.
Results are in `results/worldsdk/`. It needs a C11 compiler, Python standard
library, and an SDL2 runtime for the rendered clients. No model training,
cloud cognition or GPU is required.
The sanitizer refresh over all ten C suites is
`./scripts/sanitize-world-sdk.sh` (ASan/UBSan, clean; records in
`results/worldsdk/sanitizer.txt`).

For the normal desktop game and standalone SDK viewer, install SDL2 development
headers and run:

```sh
make all build/world_viewer
./build/cascade
./build/world_viewer build/cascade.cws
./build/world_viewer build/elek_grid.cws
```

In an environment with SDL2 runtime but no `sdl2-config`, obtain SDL2 headers and
pass `SDL_CFLAGS=-I/path/to/SDL/include SDL_LIBS=-l:libSDL2-2.0.so.0` to make.

The unchanged normal-game movement/repair/save/termination/reload replay is:

```sh
SDL_VIDEODRIVER=dummy ./build/cascade --headless --script tests/causal-before.ct --save build/replay.save
SDL_VIDEODRIVER=dummy ./build/cascade --headless --load --script tests/causal-after.ct --save build/replay.save
```

Two interactive SDK clients (three terminals):

```sh
make build/libsdk.so build/libworldview.so
python3 tools/worldsdk/server.py build/cascade.cws --save build/session
python3 tools/worldsdk/client.py build/cascade.cws
python3 tools/worldsdk/client.py build/cascade.cws
```

WASD moves; arrows turn; E uses nearby lift/portal; 1 extracts, 2 repairs, 3
transfers to the other player, 4 places a Hydro decoration in an owned room.
The server assigns the first unowned interior to a joining player. This fixture
has no integrated dialogue, economy/trade UI or complete quest. Corridor paths
are traversable by swept movement; manual visits of owned rooms still rely on
the C suite for persistence and all five offline-merge scenarios.

```sh
python3 tools/worldsdk/network_test.py
python3 tools/worldsdk/rendered_test.py
python3 tools/worldsdk/navigation_probe.py
python3 tools/worldsdk/adversarial_test.py
python3 tools/worldsdk/macro_test.py
python3 tools/worldsdk/resource_test.py
python3 tools/worldsdk/anomaly_test.py
python3 tools/worldsdk/creature_test.py
python3 tools/worldsdk/social_test.py
```

The first test uses two protocol client processes, real TCP, reconnect and a
terminated/restarted server, in three phases: the cascade entity-op phase,
then the macro Gate-4 scenario (both clients walk the derived trail, one rides the
winze lift, the open gate crosses both ways under server authority, the lode
is depleted through regional ops, both clients derive the closed gate from the
public replica, the far seat falls back to the lift, reconnect resumes, and a
restarted server restores the closed gate), then the social scenario on the
Terrace Commons fixture (one client extracts at the field, walks out of the
station's south entrance to the yard beside kyra, speaks privately — SHOW and
TELL commit canonical events that never touch the public feed — reconnects and
replays the exact command to a receipt answer with nothing re-charged, walks
the declared route to the market and files a REPORT with the guild clerk that
is not news until the canonical clock passes the dispatch delay, which the
test advances between server generations; a fresh server then publishes the
faction notice to every client through the public replica). The second uses
two actual
SDL-rendered processes. The third is the release gate over all declared walk
edges; it must pass. The fourth generates the fifteen adversarial spatial
cases and must pass with the impossible connections failing generation
cleanly. The fifth is the macro gate: it regenerates
`content/worlds/macro.json`/`macro.inc` from the seed via
`tools/worldsdk/macro.py`, verifies the committed artifacts byte-identically,
re-derives every route waypoint from the river record, proves C/Python parity
over the whole river curve and all chunk windows, checks the typed exceptions,
walks all 11 declared walk edges by swept steps, verifies the route-cost case,
and generalizes across three more seeds. `./build/macro_sdk_test` is the C side
of the same gate (4,419 checks). The sixth is the resource/ecology parity
gate; `./build/resource_sdk_test` its C side (322 checks). The seventh is the
nonlocal anomaly parity gate (440 checks); `./build/anomaly_sdk_test` its C
side (130 checks with exact literals). The eighth is the creature/NPC
parity gate (7,495 checks): committed artifacts byte-identical, species
table and identity parity, placement constraints, full time-sweep parity
per creature (dwell/travel/wrap/skip), periodicity, query parity and
determinism, exception parity with exact literals, fail-closed mutation
codes matching C, wire-v3 round trip and malformed products, plus three
generalization seeds; `./build/creature_sdk_test` is its C side (920
checks with exact literals). The ninth is the social parity gate (20,465
checks): full observer×subject view/cite parity at every step against the
C mirrors, §6.7 informs and delayed delivery with degraded confidence,
trust/restitution/decay-clock/pinned literals, private-op feed isolation,
claim-grade speech with the teller kept, fail-closed parity, a byte-exact
Python mirror of the whole v4 wire (payload, header and CRC) against the
C encoder, save/restore, and cascade generalization; `./build/social_sdk_test`
is its C side (3,814 checks with exact literals) and `./build/bridge_sdk_test`
is the legacy→canonical bridge gate (163 checks: dual-game outcome
preservation by memcmp after every operation, witness confidences, receipt
replay, hidden-truth isolation over three world variants, wire v4 round
trip and save/restore). The mapping sheet for the whole boundary is
`docs/WORLD_SDK_MAPPING.md`.
Resume tokens are local `.keys` files excluded from Git. The server only binds
loopback. Internet deployment, rate limiting and offline branch upload are absent.

## Native P4 build and deployment

Qualified **build only**: ESP-IDF v6.1 release (tag v6.1.0, toolchain
riscv32 `esp-15.2.0_20251204`, installed under the work drive at
`tools/esp-idf-v6.1` + `tools/espressif-idf6`; the earlier v6.1-dev
checkout at `f21b4c238152dc9e3a24fbad9afe33a3d15f6cfd` was replaced for
Tactility compatibility and produced a byte-identical app ELF), TactilitySDK
`0.8.0-dev` for esp32p4. The live-hardware round builds the app against
the fork-generated SDK (`repos/Tactility/release/TactilitySDK/0.8.0-dev-esp32p4/TactilitySDK`)
so its exports match the flashed firmware exactly.
Tactility now requires ESP-IDF 6; the qualified toolchain is installed under
the work drive (esp-idf-v6.1 + espressif-idf6 tools + TactilitySDK), with
`IDF_TOOLS_PATH` pointing at the IDF 6 tool root so the older v5.5.2 install
remains untouched. The SDK zip unpacks `CMakeLists.txt`, `Libraries/`,
`Modules/`, `TactilitySDK.cmake`, `version.txt` and needs an
`idf-version.txt` containing `6.1` beside them.
SDK: https://cdn.tactilityproject.org/sdk/0.8.0-dev/TactilitySDK-esp32p4.zip
Unpack into a directory named `TactilitySDK`.

```sh
export IDF_PATH=/path/to/esp-idf
export TACTILITY_SDK_PATH=/path/to/TactilitySDK
. "$IDF_PATH/export.sh"
export ESP_IDF_VERSION=6.1
scripts/build-p4.sh
```

Initialize required ESP-IDF submodules if using a shallow checkout. A minimal
external-app build used micro-ecc, mbedtls, lwip, mqtt, unity, spiffs, protobuf-c
and heap/tlsf. `IDF_SKIP_CHECK_SUBMODULES=1` only avoids unrelated automatic
submodule downloads; it does not replace the required sources.

The checked-in compressed ELF is
`releases/worldsdk/cascadeterrace.app.elf.gz`. Exact byte counts, hashes, section
sizes and compiled-source hashes are in `results/worldsdk/p4-build.json`.
`deploy-cm5.sh` verifies/decompresses this artifact when no local build exists.
With the physical device's Tactility development server enabled:

```sh
CASCADE_MODE=0 CASCADE_QUALIFY=1 scripts/deploy-cm5.sh P4_HOST
```

This installs/runs the existing app; it does not flash unrelated firmware. No
physical device was used here. The packaged app uses the recipe site adapter but
retains normal gameplay and mode-0 dialogue. SDK-only functions compile into
component objects; unused functions are discarded from the default external ELF.
The Python multiplayer server/client are desktop fixtures, not P4 transport.
A native P4 SDK-mode client remains to be integrated and measured.

## Architecture map and next work

- `world/sdk.h`, `schema.c`: IDs, versions, compact records, CRC, local addresses.
- `recipe.c`: bounded decode, validation, materialization, portal graph search.
- `geometry.c`, `traversal.c`: shared boxes, room door openings, collision, ground,
  sampled witness confidence, scope transition and timed lift movement.
- `state.h`, `state.c`: verified actions, inventory/ownership, directed promise
  reliability, public feed, personal objective watermarks, deterministic merge.
- `resource.c`: regional reservoirs — deterministic weather, drawdown-aware
  river stage, rate-based recovery with pit healing, kind-aware extraction
  (EXCAVATE/CONVERT/SPEND), the conservation ledger identity, and the
  nonlocal anomaly gate (ws_link_open, ws_use_link_state, ws_route_cost_state:
  open iff 2×level ≥ capacity, instant state-gated crossing, closed gates
  absent from routing).
- `creature.c`: sparse deterministic creatures and NPCs — stable
  ancestry/species/slot identities (`ws_creature_id` from `ws_child_id`),
  pure placement from the recipe alone (anchored slots on the anchor walk
  surface, fauna inside their biome reservoir on the terrain top, never
  blocked), access-graph station sets (BFS ≤ 2 hops over walk links),
  route-cost-weighted dwell/travel schedule cycles as pure functions of
  (identity, now) with route-polyline interpolation, window queries
  materializing sparse offscreen entities in deterministic (species, slot)
  order with a cap contract, and authority-side persistent exceptions
  (DEAD/RELOCATED/PINNED) overriding generated defaults, fail-closed.
- `persistence.c`: explicit wire codec, semantic hash, two-slot save/recovery;
  conditional version-2 resource section (schema-1 states unchanged);
  conditional version-4 social section (evidence/pending/receipts).
- `social.c`: directed evidence capsules, witness projection bookkeeping,
  hearsay degradation and time decay, conduct views and citations,
  institutional filing with delayed delivery, and the canonical clock
  advance for schema-1 worlds.
- `content/worlds/`: human source plus compiled default Cascade include.
- `content/cascade_adapter.c`: intentional game-specific role mapping. The SDK
  has no character or mystery names. Legacy terrain/mystery code remains intact.
  Gate VI adds the `Bridge` canonical event boundary (`bridge_init`,
  `bridge_apply`): the legacy game stays the gameplay authority while the
  same acts cross into the authority state as versioned events.
- `core/render.c`: shared software rasterizer plus SDK geometry/peer entry points.
- `tools/worldsdk/`: finite source compiler, inspection, generators, probes,
  authority transport and rendered clients. No hidden assets or trained models.

**Resource/ecology gate: PASSED.** Four geography-derived regional
reservoirs (massif terrain, lake-country water, forest biomass, karst Phos)
ride the macro product as schema 3 (64-byte records, fail-closed validation,
same-kind overlap rejected). Extraction depletes the regional stock and
writes sparse persistent sites: intentional excavations (foundations, cave
entrances) never heal, disturbance pits heal out of the recovering stock
with the settled matter accounted as buried. Material converts to Geo-phos
shards bounded and lossy both directions (3:1 refine, 2:1 deposit). Water
drawdown is visible on the existing river records through `ws_river_stage`
(width/depth scale with the level, lake/wetland reaches dry to a marsh band
below a quarter and a dry bed at zero) with no fluid simulation; the
renderer binds live state via `render_bind_state`. Recovery is rate-based
from deterministic weather (clear/rain/storm from seed+day), zone class and,
for biomass, the paired overlapping Phos stock; inflow is capacity-capped.
A conservation ledger identity (sum of levels + carried + used + lost ==
initial + recovered) is enforced by `ws_state_validate` after every
operation and tick, across chunks, compaction and save/restore. Resource
ops bind to the global revision, are region-local for clients, never merge
offline, and reject dust/duplicate/overflow attempts transactionally. The
macro product is now 1,938 bytes; the C proof gate runs 322 checks with
exact literals, the Python parity gate 1,114; schema 1/2 products and
schema-1 state wires remain byte-compatible with every published artifact.

**Nonlocal anomaly gate: PASSED.** The anomaly is product vocabulary: link
kind 3 with the anchor reservoir index in the link's reserved byte (zero
for ordinary links, so no wire format changes). The anchor must be a Phos
reservoir; the gate's seat must lie inside the anchor region (2D) with its
far end outside and at least three ordinary hops away (ordinary_hops BFS,
gates excluded); at most one gate per Phos lode; unknown kinds and
non-anomaly reserved values are rejected. Activation is a pure function of
the canonical state — open iff 2×level ≥ capacity (the karst lode closes
between 80,000 and 79,999; NULL/empty state answers declared levels). The
stateless APIs stay frozen: `ws_use_link` skips gates, `ws_link_cost`
answers the 1,500 toll only under CAP_ANOMALY, `ws_reachable` classifies
kind 3 as capability-gated. The state-aware APIs bind the canonical state:
`ws_use_link_state` crosses an open gate instantly at the seat (locked
mid-travel, falling back to the ordinary lift when closed),
`ws_route_cost_state` omits closed gates, `ws_link_open` is the pure
openness predicate mirrored in Python. The committed gate links the forest
ruin to the karst Phos plane (derived from the ridge center) across ~473 m
of unchanged geography, with the winze lift (`trail_e3`↔`phos_ruin`, cost
3,007) as ordinary access; open, ruin→haven halves to 1,545,498 through
the gate; closed, every ordinary literal is bit-identical to the
pre-anomaly product. Depleting the lode through the ordinary regional op
path (to 28,930) removes the shortcut live for both clients; recharge
reopens it day by day; save/restore carries the closed state; the server
binds `use` to `ws_use_link_state` and publishes reservoir levels in the
public replica so clients derive the same gate state. The C proof gate
runs 130 checks with exact literals, the Python parity gate 440 (openness
sweep, toll/routing parity, depletion/recharge, wire round trip, probe
products, three generalization seeds), and the two-client macro network
scenario 2,103 checks. The macro product is 1,938 bytes (22 modules, 13
links: 11 walk, 1 lift, 1 gate); deterministic qualification now rejects
7,372 malformed products.

**Creature/NPC gate: PASSED.** Creatures are product vocabulary: schema 4
appends 32-byte species records (id, kind FAUNA/NPC/REQUIRED, habitat =
reservoir index for fauna or anchor module index for anchored, slots,
period, stations, radius, seed) after the reservoir table; `ws_validate`
enforces kind-specific rules (fauna habitats must be reservoirs, anchored
habitats must be exterior walk surfaces, REQUIRED habitats must be
baseline-walk-reachable from the first walk module — probed at compile
time with `route_bfs_probe`), bounded slots/stations/period/radius and
unique identities, all fail-closed. Identity is stable and
ancestry-derived: `ws_creature_id = ws_child_id(species_id, slot+1,
0x4352)`, so the same source regenerates the same creatures forever.
Placement is solved at query time from the recipe alone — no tick replay,
no stored positions: anchored slots land on the anchor walk surface within
the extent margin and never blocked; fauna slots land inside their biome
reservoir extent on `ws_ground`/terrain top. Stations come from the
access graph (BFS over walk links, ≤ 2 hops, lowest (hop, index) wins);
fauna take further biome points within their radius. Schedules are pure
functions of (identity, now): a per-slot phase offset cycles WS_CRE_DWELL
(64 s) plus route-cost-weighted travel (`route_cost/WS_CRE_SPEED` for
anchored, chord/SPEED for fauna), interpolated along the actual route
polyline with 16.16 fixed-point truncation — cross-chunk travel rides the
existing topology, chunk unload/reload changes nothing, and time skips
resolve instantly. `ws_creature_query` materializes a window in
deterministic (species, slot) order with a bounded cap (negated on
overflow), never touching the state; `ws_creature_except` is the only
mutation path (authority-only, fail-closed: unknown ids, wrong kind for
the aux payload, uniqueness enforced), overriding placement for DEAD
(removed), RELOCATED (fixed new position, aux-checked) and PINNED (home
station). Exceptions persist through wire v3 (state version bumped,
schema-1/v1/v2 wires unchanged and still accepted). The committed macro
product now carries eight species / 36 slots (massif, lake, forest, karst
fauna; the required gate warden and two gate residents at high_gate; the
required haven wardens and three haven residents at ford_haven) in 2,196
bytes with every Gate I–IV literal bit-identical (ruin→haven still
2,668,102). The C proof gate runs 920 checks with exact literals
(pinned-home placements for all 36 slots with constraint verification
against the engine's own collision/ground, schedule segments,
route-collinearity with truncation slack, periodicity and clock wrap,
query order/cap/determinism, exception behaviors and fail-closed codes,
wire-v3 round trip, mutated-load rejection including duplicate identity
with the control case proving non-required karst anchors remain legal);
the Python parity gate runs 7,495 checks (full C/Python parity over every
creature and a dense time sweep including 32-bit wrap, plus three
generalization seeds); deterministic qualification now rejects 7,650
malformed products.

**Canonical event/social gate: PASSED.** The boundary is the bridge in
`content/cascade_adapter.c`: `bridge_init` checks the Cascade cast
(station 4, market 6, kyra 15, waterfall field 9) and `bridge_apply` runs
the legacy game first — the gameplay authority — then its canonical echo,
with four honest statuses (COMMITTED, DENIED, UNMAPPED, REFUSED) and a
bounded vocabulary: REPAIR/DAMAGE map to the station, EXTRACT to the
field, SHOW/TELL/TRANSFER/GIVE/BUY/SELL-with-kyra to the canonical
SHOW/TELL/EXCHANGE, and the promise lifecycle to WS_PROMISE with
kept/breach evidence on the promise_state 1→2/1→3 transitions (teller =
the player for promise speech, 0 for authority-observed facts); market
trades with non-person counterparties, notes, and time mechanics stay
UNMAPPED rather than being fictionally mapped. The C gate proves the
legacy `State` is byte-identical (memcmp after every operation) whether
or not the bridge is attached, across all three fixture variants — so the
canonical layer adds observation without touching gameplay, and the
hidden-truth case shows three worlds with different secrets producing one
identical canonical hash. Canonical commits carry one revision watermark
and retry receipts (replays answer from the receipt with no repeated cost
or reward; fingerprint mismatches are stale reuse, not retries). After
each commit the authority projects the act onto actual NPC witnesses
through the engine's own witness gate — distance, scope and 128-sample
occlusion — so kyra at 940, dax/marisol at 790 witness the station
damage, oren out of range and toma occluded stay exactly uninformed, and
the station-edge literal is 900. Retained evidence capsules
(`WsEvidence`, 64-cap) carry observer/subject/teller/root, epistemic
class, confidence, context, valence, salience and the canonical clock
anchor; hearsay degrades to 3/4 capped at 750 and can never rise through
copying; decay is a fixed integer bucket (86,400 s) with salience ≥ 500
pinned forever; the same root retold replaces instead of stacking (the
§6.7 exchange case: kyra's plain projection is replaced by the exchange's
own record, acts 7, trust 481). Conduct projections (`ws_social_view`)
are pure functions of (records, clock) — counts by class, decayed
confidence capped at 1000, trust bounded ±1000 and a revision watermark —
with arena/defense halving and restitution doubling evaluated at view
time so history is never rewritten; `ws_social_cite` hands dialogue the
strongest actual cause. Private speech (SHOW/TELL/EXCHANGE) commits as
`WS_QUIET`: canonical history, never public news. REPORT files the
observer's strongest retained evidence with a clerk NPC
(`ws_file`); delivery waits for the canonical clock to pass
`WS_REPORT_DELAY_S` (3600 s), settles at the next authority touch
(`ws_social_settle`, driven from `ws_apply`, `ws_inform` and the resource
tick, plus `ws_clock_advance` for schema-1 worlds) and publishes the
faction notice at 3/4 degraded confidence. NPC↔NPC and authority-mediated
information flows through `ws_inform` with the same record machinery
(self-reports rejected, roots must reference committed revisions). The
wire gains a conditional version-4 section (evidence/pending 34 B each,
receipts 28 B per player; v1/v2/v3 unchanged without social records —
`ws_state_hash` is the CRC32 of the tail-less payload, pinned by a
byte-identical Python mirror of payload, header and CRC). The fixture is
Terrace Commons (`content/worlds/social.json`, 23 modules, 1,592 bytes):
the station/market/field cast, dax and marisol in witness range, oren
ranged out, toma occluded behind the north wall, the clerk at the market,
and a walkable yard beside kyra with a declared station↔yard walk link so
protocol clients reach private-speech range. The C proof gate runs 3,814
checks with exact literals; the bridge gate 163; the Python parity gate
20,465 over every observer-subject pair at every step; the two-client TCP
scenario (2,385 total) proves the private feed untouched, the receipt
replay after reconnect, and the delayed faction notice published by a
fresh server after the clock passes. The full mapping sheet — conceptual
field → actual SDK field → unit → version → migration → test, with typed
time, rounding and RNG frozen — is `docs/WORLD_SDK_MAPPING.md`.

**The bounded renderer evaluation the mission ordered at the end of Gate 4
is complete** — measured, not speculated, with all evidence in
`results/worldsdk/renderer-eval/` and the full write-up in
`docs/RENDERER_EVALUATION.md` (harness in `tools/renderer-eval/`). Verdict:
**HYBRIDIZE** — keep the current renderer's architecture and per-pixel
z/fog/fidelity semantics, take Jet's incremental span-rasterization
technique for the inner loop (its raster core measured 2.9-7.2x faster on
identical data; adopting the library itself would cost +90-94 KB .text and
+18 KB internal SRAM against ~14 KB of ELF headroom, loses sub-pixel
triangles — motes are first-class content — and carries a real
`colorBaked` shared-material bug), and move presentation to the proven
P4 PIE kernels (assembled with the production `xespv` march, 96 bytes of
.text) and/or async PPA SRM (Espressif CI floor implies ~2-2.5 ms
off-CPU for the 2x upscale).

**The proving integration is done and promoted** — implemented and
qualified on `work/cascade-renderer-hybrid` (Stage A `8bbe12e`:
conservative incremental spans in `core/render.c` with exact tested depth
and coverage, pixel-identical against the `ca942d3` reference across the
hybrid qualification corpus; Stage B `b1229f0`: `core/presentation.c`
exact portable 2x expansion plus gated PIE (`main/presentation_pie.S`)
and PPA (`main/presentation_p4.c`) backends; the save-header regression
fix `e835061` precedes them), merged linearly into this branch, then
reconciled in the containing commit:

* the presentation backend opens exactly once during normal app
  initialization — after the scanout allocation, before the initial
  memory/qualification telemetry (so `ct_present_extra_bytes` reports the
  actually opened backend) — and Save only saves; repeat calls to
  `ct_present_open` are no-ops so no path can re-register or leak PPA
  resources, and `ct_present_close` remains on every shutdown/error path.

Production policy: **incremental-span rasterizer promoted; portable
exact-2x presentation default/promoted; PIE experimental pending physical
execution/parity; PPA experimental pending firmware-export verification
and physical timing/visual/cache/display qualification.** No further
renderer or PPA research before physical P4 testing (see
`docs/RENDERER_HYBRID.md` and `results/worldsdk/renderer-hybrid/`).

After that, the next work is the multiplayer gate — replacing the separate
fixture with an experimental normal-game multiplayer mode over the now-proven
canonical event boundary (versioned commands, receipts, witnesses and social
projections are the multiplayer substrate), then implementing P4 network
transport — in that order, each under its own gate. The spatial foundation
enforces
topology-before-geometry (declared walk edges cut 4000 mm ports into room
walls; every room keeps a default public south entrance), the surface
convention (`pos.y` is the walkable top), corridor locality (overlapping
bands resolve to the nearest centerline, exact ties to the higher deck),
capability-aware reachability (`ws_reachable`:
WALK/ABILITY/CONDITIONAL/INACCESSIBLE/INVALID) and hard constraints with
compiler parity (`ws_topology` + `validate_walk_edge`: one local frame per
edge, legal port fit, ramp steps ≤ 800 mm, slope ≤ 45°, no unrelated solid
on the direct route, NPCs need baseline public access). Keep
`navigation_probe.py`, `adversarial_test.py` and `macro_test.py` green
while adding content; do not add exceptions for named modules.

Expand NPC profiles/schedules and observation
provenance only after the authority and traversal boundaries are sound.
Geometry fidelity labels do not yet implement cold storage streaming, NPC
simulation LOD, or full room chunk eviction.

The SDK witness function is a distance/attention/conspicuousness gate with
coarse 128-sample occlusion; since Gate VI it feeds NPC knowledge records
(`witness_project` → `WsEvidence`), but it still does not model sound and
its occlusion is coarse, so do not describe it as a complete perception
system. Directed social state covers observed acts, claims, promises,
kept/breach, restitution and institutional reports with decay — not a
universal reputation score, and no morality scalar.

## Add recipes and validate safely

Run `sdk.py schema`, copy a source JSON, retain immutable ancestry for updates,
and assign stable unique sibling keys. New worlds receive new ancestry. Names
are presentation; keys and parent IDs carry identity. Existing shapes/tags/Phos
are CONTENT changes. Unknown fields/tags/Phos fail closed and require an explicit
SCHEMA_EXTENSION design/version change. ENGINE changes include physics/network
or persistence rules. Do not edit compiled `.inc` files by hand.

```sh
python3 tools/worldsdk/sdk.py validate content/worlds/example.json
python3 tools/worldsdk/sdk.py compile content/worlds/example.json --output build/example.cws
```

Compiler validation currently certifies types/IDs/bounds/topological connectivity
and, with bit-exact runtime parity, the realizability of every declared walk
edge: local frames, legal ports, enterable ramp steps, walkable slopes,
unblocked direct routes and baseline public access for NPCs. It does not
certify continuous gameplay beyond the probe's swept-edge walks; run new
content through the navigation probe and adversarial harness before any
release. Changed recipe hashes require explicit compatibility or migration
work; never silently reinterpret old checkpoint data. The v1 save fixture is
regenerated whenever the recipe CRC changes (decode against the old product,
re-bind the CRC, re-encode); never hand-edit it.

## Physical validation: first live-hardware round DONE (2026-09-24), remainder required

Device A (Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5, the user's important
device) runs the ag1357 fork firmware `work/waveshare-p4-audio-exports`
@ `e423c281` on ESP-IDF v6.1 release (riscv32 `esp-15.2.0_20251204`;
the fork SDK at `repos/Tactility/release/TactilitySDK/0.8.0-dev-esp32p4`).
A full pre-change backup of the device's mutable state lives at
`/media/cloud/2982-E16B/tactility_p4_work/backups/device-a/2026-09-24/`
(65 files: all four external apps, every settings/user-data tree, crash
log, SHA-256 manifest; restore via `/fs/upload`). The game installs as an
external ELF through the web API (`PUT /api/apps/install`, port 80) and
runs with **zero unresolved symbols** against the firmware export table.

Measured on device (single instance, portable presentation, evidence in
`results/worldsdk/p4-physical.jsonl` and the live serial capture):
generation 36 ms; explicit PSRAM 496,156 B (Game + Renderer + canvas +
presentation); heap internal free 129,691 B (min watermark 92,440, largest
block 51,200), PSRAM free 32.44 MB (min 31.49 MB), 33 tasks; render p50
136.6 ms / p95 139.2 ms / max 140.9 ms at 818 triangles (landscape frame;
the portrait frame renders ~1,098 triangles in 120-153 ms); frame period
p50 175 ms (~5.7 fps, render-bound at 92% of frame work); portable 2x
presentation submit+poll p50 4.47 ms / p95 4.64 ms; cognition probes
1.5-2.1 ms with honest abstention on unknowable questions; save 7.1-8.3 ms;
**save/reload across reboots proven on device** (`reload: 1` boot lines
after `cascade.save.1` exists); touch functional (user-confirmed), input is
keyboard-first (CardKB2 BLE/USB HID via the fork's HID host; the app
creates no software keyboard). Display fit fixed this round: the panel's
LVGL space is 320x480 portrait, so the game now renders 160x240 internally
(same 38,400-px budget and buffer sizes) with the projection derived from
W/H (horizontal half-fov preserved), the 2x presentation filling the panel
exactly — verified by screenshot analysis (no bars, content edge to edge;
`results/worldsdk/p4-physical-screenshot.png`).

Device findings (firmware-side, recorded for the fork mission, not fixed
here): (1) the webserver's `/api/apps/run` deliberately starts instances
ALONGSIDE the running app ("no stop the existing one first" in
`WebServerService.cpp`; the README's stop-first claim is stale), and there
is no remote close — repeated runs stack instances, and stacked
CPU-bound instances starve IDLE0 and trip the task watchdog (observed;
recovery by reset); a single instance runs clean with no watchdog for the
whole session. (2) The user reports the CardKB2 BLE keyboard must be
manually re-paired after every device reset — a bonding-persistence issue
in the fork's BLE HID host, queued for that mission. (3) Opening
`/dev/ttyACM0` may pulse the device's auto-reset (known); ModemManager
must be stopped for serial work and restarted after.

App-side hardening from this round: the telemetry stream is now durable
and live-readable (each record closes/reopens the append file because the
firmware exports no `fsync` and FATFS keeps the dirent size stale until
close), and a low-cost monitor task reports loop progress (frame, stage,
iteration time) from outside the main loop every ~2 s — both proven on
device. All C gates re-run green after the portrait change (traversal 56,
macro 4,419, resource 322, anomaly 130, creature 920, social 3,814, bridge
163, authority-merge 3,873; legacy 864/0; presentation 9,395; causal
before/after replays pass).

Keyboard controls (added after the user's first hands-on round; deployed
and smoke-verified, physical key testing still pending): W/A/S/D movement
(W forward, S back, A/D strafe), O/P camera turn left/right, I toggles a
first-person eye-level view against the default chase view (presentation
state only, never saved), U interacts (opens/leaves the conversation view;
Esc also leaves it). The kernel keyboard stream is per-key press/release
events, but LVGL's keypad pipeline collapses them to one-shot KEY events
(its indev reports RELEASED whenever the driver queue is empty), so
hold-to-move and chords are impossible through the group: while the game
view is active the app latches the stream itself — every LVGL keypad indev
is disabled each frame (a disabled indev stops calling its read callback,
so the device queues back up; keyboards connecting mid-run are covered by
the per-frame walk) and each `KEYBOARD_TYPE` device is drained through the
exported `keyboard_read_key` for true held-key state. While talking, the
latch is released so keys type into the textarea through LVGL again; on
app close the latch is always released. Trade-off: an app crash would
leave the keypad indevs disabled until reboot. Touch turn and the desktop
arrow keys were sign-inverted against the yaw convention (+turn is
clockwise/right) and are fixed to match O/P. Desktop gains I and U for
parity. Measured on device after the change: generation 39.3 ms, reload 1,
explicit PSRAM 496,160 B (+4 for the view field), loop ~132 ms, no
watchdog; new app undefineds beyond the prior build are exactly seven
(keyboard_read_key, KEYBOARD_TYPE, device_for_each_of_type,
lv_event_get_key, lv_indev_enable/get_next/get_type), all verified in the
flashed firmware's symbol table.

Still required: stack high-water on a clean app close (the remote close
gap above blocked exercising the close path; the instrumented close-path
telemetry is in place for the next round); power-loss/torn-save testing;
prolonged thermal soak; repeated save/reload under play; native network
disconnect/reconnect and simultaneous players on device; PIE/PPA physical
qualification — these are firmware-build variants (`CT_PRESENT_PIE/PPA`)
and the PPA backend additionally needs firmware-export verification
(`ppa_*` symbols are not app-visible), so they need an explicit flashing
decision on Device A, not an app install. Audio is unimplemented. Desktop
timings are not physical P4 measurements.
