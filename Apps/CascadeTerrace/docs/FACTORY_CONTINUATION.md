# Factory continuation: World SDK foundation

Repository: ag1357/TactilityApps. Branch: `work/cascade-world-sdk`.
Starting commit: `f216216d4b2f407a7684dcfb455833b116a7de1b`.
This handoff describes its containing checkpoint. Resolve the exact current
checkpoint with `git rev-parse HEAD` after fetching this branch; do not start
again from the cognition branch. No unpublished context is required.

**Status: FIFTH IMPLEMENTATION GATE PASSED.** The SDK continuous-traversal
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
preserved bit-for-bit. The normal Cascade game
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

## Build and reproduce

All commands below run from `Apps/CascadeTerrace`.

```sh
make test
python3 tools/worldsdk/sdk.py schema
./scripts/qualify-world-sdk.sh
```

The qualification script now exits **0** with the traversal, adversarial,
macro-geography, resource/ecology, nonlocal anomaly and creature gates
green. Do not
change it to ignore a failure if one reappears; it must exit nonzero when
any gate fails.
Results are in `results/worldsdk/`. It needs a C11 compiler, Python standard
library, and an SDL2 runtime for the rendered clients. No model training,
cloud cognition or GPU is required.

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
```

The first test uses two protocol client processes, real TCP, reconnect and a
terminated/restarted server, in two phases: the cascade entity-op phase, then
the macro Gate-4 scenario (both clients walk the derived trail, one rides the
winze lift, the open gate crosses both ways under server authority, the lode
is depleted through regional ops, both clients derive the closed gate from the
public replica, the far seat falls back to the lift, reconnect resumes, and a
restarted server restores the closed gate). The second uses two actual
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
checks with exact literals).
Resume tokens are local `.keys` files excluded from Git. The server only binds
loopback. Internet deployment, rate limiting and offline branch upload are absent.

## Native P4 build and deployment

Qualified **build only**: ESP-IDF v6.1-dev, commit
`f21b4c238152dc9e3a24fbad9afe33a3d15f6cfd`, riscv32 toolchain
`esp-15.2.0_20250929`, TactilitySDK `0.8.0-dev` for esp32p4.
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
  conditional version-2 resource section (schema-1 states unchanged).
- `content/worlds/`: human source plus compiled default Cascade include.
- `content/cascade_adapter.c`: intentional game-specific role mapping. The SDK
  has no character or mystery names. Legacy terrain/mystery code remains intact.
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

After that, the next work is bridging the existing game's
Phos/evidence/economy/quest actions into this single semantic authority,
replacing the separate fixture with an experimental normal-game
multiplayer mode, and implementing P4 network transport — in that order,
each under its own gate. The spatial foundation
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

The SDK witness function is a distance/attention/conspicuousness gate with coarse
128-sample occlusion. It is not yet connected to NPC knowledge records and does
not model sound. Do not describe it as a complete witness system. Directed social
state currently handles promises/reliability only; no universal reputation score.

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

## Physical validation still required

Measure loader compatibility; internal RAM versus PSRAM allocation; stack high
water and heap fragmentation; render p50/p95/max; input/touch/CardKB2; storage
stalls; repeated save/reload and actual power loss; prolonged thermal behavior;
then native network disconnect/reconnect and simultaneous players. The PPA
presentation backend is implemented but experimental: it still needs
firmware-export verification and physical timing/visual/cache/display
qualification before promotion (PIE likewise needs physical execution/parity).
Audio is unimplemented. Desktop timings are not physical P4 measurements.
