# World SDK continuation

Starting commit: f216216d4b2f407a7684dcfb455833b116a7de1b.
Continuation branch: work/cascade-world-sdk. Schema 1, generator 1.
Default dialogue remains mode 0. Frozen cognition experiments are untouched.

## Bounded design review

| Reference | Adopted | Not imported |
|---|---|---|
| [Unreal World Partition](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine) | Bounded scope fidelity and distance selection | Editor, global streaming runtime, HLOD builder |
| [Gameplay Tags](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-tags-in-unreal-engine) | Versioned finite source registry compiled to IDs | Dynamic string matching on device |
| [Gameplay Ability System](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-ability-system-for-unreal-engine) | Legal actions over attributes, explicit effects | Ability framework and reflection |
| [Smart Objects](https://dev.epicgames.com/documentation/unreal-engine/smart-objects-in-unreal-engine---overview?lang=en-US) | Typed affordances and reservations | Unreal object runtime |
| [StateTree](https://dev.epicgames.com/documentation/en-us/unreal-engine/state-tree-in-unreal-engine) | Small deterministic behavior states | Behavior editor and scripting VM |
| [Godot scenes](https://docs.godotengine.org/en/stable/tutorials/best_practices/scene_organization.html) | Separate content, services and platform | Scene graph allocation per entity |
| [O3DE asset pipeline](https://www.docs.o3de.org/docs/user-guide/assets/pipeline/) | Human source versus validated runtime product | Asset daemon and engine dependencies |
| [Recast/Detour](https://recastnav.com/) | Bounded connectivity and explicit off-mesh links | Voxel bake on device; initial module/portal graph is not a Recast mesh |
| [Godot multiplayer](https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html) | Server authority and explicit peer state | Engine RPC framework |
| [ESP-IDF external RAM](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/external-ram.html) | Explicit bounded workspaces, account internal versus external allocations | Assumed uniform RAM latency |
| [ESP-IDF PPA](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/ppa.html) | Candidate scaling/blending service | Claim that PPA is a 3D GPU; no unmeasured acceleration |
| [ESP-IDF FATFS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/storage/fatfs.html) | CRC and recoverable generations above platform file service | Claims of physical power-loss qualification |
| [ESP-IDF lwIP](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/lwip.html) | Transport separated from semantic replication | Assumed radio/network availability on P4 |

These are conceptual choices, not ports or performance claims. No third-party
runtime code is introduced. Research ends here; subsequent decisions use tests.

## Canonical boundary

`world/sdk.h` is the bounded C contract. Human recipes compile on the host to
explicit little-endian products. Runtime structs are never wire formats.
128-bit IDs derive from immutable ancestry, stable local keys and a generator
version. They are deterministic identifiers, not cryptographic authentication.
At this scale 128 bits are adequate; validation also rejects collisions. 256-bit
identities would double every handle without addressing malicious inputs.
Recipe hashes and CRC are consistency checks, never authorization proofs.

Positions are signed integer millimetres within a scope. Products carry schema,
generator, epoch and revision. Unsupported schema/extension IDs fail closed.
State reconstruction combines immutable product, resolved exceptions and a
bounded recent tail. Unique ownership/inventory and reward history cannot be
losslessly compressed to a seed; server archival history may grow.

The SDK accepts no dialogue world-truth bypass. Observations must cross the
existing authorized NPC view boundary. Server operations are verified before
canonical mutation; private player summaries are not public reputation.

## Integration policy

Compile and test each boundary before publishing it. Preserve legacy save and
terrain generator version 1. Move content data behind adapters incrementally;
do not silently reinterpret old save seeds. New traversal/replication modes
must remain explicitly selectable until qualified. Exact implemented features,
limits, commands and measurements belong in FACTORY_CONTINUATION.md and the
machine-readable results. An API declaration is not an implemented feature.
