# Factory continuation: World SDK foundation

Repository: ag1357/TactilityApps. Branch: `work/cascade-world-sdk`.
Starting commit: `f216216d4b2f407a7684dcfb455833b116a7de1b`.
This handoff describes its containing checkpoint. Resolve the exact current
checkpoint with `git rev-parse HEAD` after fetching this branch; do not start
again from the cognition branch. No unpublished context is required.

**Status: FIRST IMPLEMENTATION GATE PASSED.** The SDK continuous-traversal
promotion gate passes 18/18 declared walk edges, and the adversarial spatial
suite passes 15/15 with impossible connections failing generation cleanly.
The normal Cascade game remains playable and passes its original suite. Macro
geography, resource/ecology proof, the nonlocal Phos edge, the complete
multiplayer quest and all physical P4 claims remain pending. The two-client
SDK fixture is not the full Kyra/intake multiplayer game.

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
| I: spatial/access repair, 18/18 traversal, adversarial suite | containing commit of this update; resolve with `git rev-parse HEAD` |

## Build and reproduce

All commands below run from `Apps/CascadeTerrace`.

```sh
make test
python3 tools/worldsdk/sdk.py schema
./scripts/qualify-world-sdk.sh
```

The qualification script now exits **0** with the traversal and adversarial
gates green. Do not change it to ignore a failure if one reappears; it must
exit nonzero when any gate fails. Results are in `results/worldsdk/`. It needs
a C11 compiler, Python standard library, and an SDL2 runtime for the rendered
clients. No model training, cloud cognition or GPU is required.

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
```

The first test uses two protocol client processes, real TCP, reconnect and a
terminated/restarted server. The second uses two actual SDL-rendered processes.
The third is the release gate over all declared walk edges; it must pass. The
fourth generates the fifteen adversarial spatial cases and must pass with the
impossible connections failing generation cleanly.
Resume tokens are local `.keys` files excluded from Git. The server only binds
loopback. Internet deployment, rate limiting and offline branch upload are absent.

## Native P4 build and deployment

Qualified **build only**: ESP-IDF v6.1-dev, commit
`f21b4c238152dc9e3a24fbad9afe33a3d15f6cfd`, riscv32 toolchain
`esp-15.2.0_20250929`, TactilitySDK `0.8.0-dev` for esp32p4.
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
- `persistence.c`: explicit wire codec, semantic hash, two-slot save/recovery.
- `content/worlds/`: human source plus compiled default Cascade include.
- `content/cascade_adapter.c`: intentional game-specific role mapping. The SDK
  has no character or mystery names. Legacy terrain/mystery code remains intact.
- `core/render.c`: shared software rasterizer plus SDK geometry/peer entry points.
- `tools/worldsdk/`: finite source compiler, inspection, generators, probes,
  authority transport and rendered clients. No hidden assets or trained models.

**Next gate: macro geography.** Express, through compact recipes and seeded
modifiers only, at least a mountain/valley region, plains region,
river/watershed, lake or wetland, forest distribution, cave, ruin, two
settlements and a wilderness route between them. Prove the hierarchy
(world/region/settlement/district) with the same engine: no settlement-specific
logic. The spatial foundation now enforces topology-before-geometry (declared
walk edges cut 4000 mm ports into room walls; every room keeps a default public
south entrance), the surface convention (`pos.y` is the walkable top),
corridor locality (overlapping bands resolve to the nearest centerline, exact
ties to the higher deck), capability-aware reachability
(`ws_reachable`: WALK/ABILITY/CONDITIONAL/INACCESSIBLE/INVALID) and hard
constraints with compiler parity (`ws_topology` + `validate_walk_edge`: one
local frame per edge, legal port fit, ramp steps ≤ 800 mm, slope ≤ 45°, no
unrelated solid on the direct route, NPCs need baseline public access). Keep
`navigation_probe.py` and `adversarial_test.py` green while adding content;
do not add exceptions for named modules.

After that: the resource/ecology conservation proof, the nonlocal Phos edge,
then bridging the existing game's Phos/evidence/economy/quest actions into
this single semantic authority, replacing the separate fixture with an
experimental normal-game multiplayer mode, implementing P4 network transport,
and qualifying it physically. Expand NPC profiles/schedules and observation
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
then native network disconnect/reconnect and simultaneous players. PPA acceleration
and audio are unimplemented. Desktop timings are not physical P4 measurements.
