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

## Implemented products and limits

The C runtime is allocation-free during geometry, traversal and semantic actions.
The save service temporarily allocates a 12,000-byte wire buffer and (on restore)
a 9,280-byte staging state. Product materialization is bounded to 128 modules and
256 links; this is not a cold-store streamer. Distance fidelity currently controls
geometry submission, not storage eviction or full NPC simulation.

Product wire layout: 48-byte header (CWS1, schema/generator u16, length/CRC u32,
ancestry 128 bits, seed/epoch/revision u32, module/link counts u16), then 64-byte
modules and 8-byte links. All integers are little endian. The high byte of kind
holds hierarchy level; its low byte holds primitive type. A module's position
is in the exterior scope, except explicit interiors, which have local origins.
Hierarchy ancestry does not imply nested transform multiplication.

Checkpoint layout: 48-byte versioned header, 28 bytes per resolved entity,
538 bytes per player, 12 bytes per directed player relationship, 16 bytes per
feed item, 20 bytes per recent operation. Counts and CRC precede bounded decode.
Magic is shared with the recipe family; exact layout/count validation and the
bound recipe CRC disambiguate products. Unsupported versions fail closed; no
migration between different recipe hashes is implemented. The v1 checkpoint
fixture is checked in. There is no raw C struct serialization.

128-bit identity is deterministic. CRC32 detects accidental corruption; it is
not a cryptographic content or authorization proof. Host manifests additionally
carry SHA-256. The loopback server authenticates resumed player IDs with random
local tokens, binds operations to a recipe hash and supplies the actor identity.
Clients cannot assert server-authority flags. This is not an Internet-ready
transport: TLS, movement rate limits and deployment identity provisioning are
pending. Do not expose the prototype beyond loopback.

Semantic operations and transient movement use separate request paths. Full
public semantic snapshots are sent when the revision changes; unchanged movement
responses omit the world payload. Private directed relationship vectors are never
replicated. Public hashes cover public semantic state, not private inventories,
behavior vectors or transient positions. Reconnect requires a matching product.
Personal epoch watermarks make completion rewards monotone and idempotent.
They do not preserve every accomplishment as an archival event stream.

Offline merging accepts only server-retained base checkpoints and two explicitly
implemented conditional operations: repair and aid. Scarce extraction, transfers
and property changes are online-authoritative. Network upload/authentication of
offline branches is not implemented. A later destruction epoch wins over a stale
repair; later NPC death wins over earlier aid while legitimate personal credit
can survive. Neither AI nor generated dialogue resolves these conflicts.

Compaction resolves supported operations into entity/player exceptions while
retaining only a bounded tail. The benchmark exercises a fixed population, room
decoration, property assignment, resource transfers, repairs and promises. It
proves exact state reconstruction for those operations, not compression of
arbitrary unique content, arbitrary population growth, or a global historical
archive. Saved current fields are not inferred from descriptive damage classes.

## Failed integration gate

`navigation_probe.py` uses actual swept collision and ground queries. Only 1/18
original declared walk edges passes direct traversal. Graph connectivity alone
was insufficient: room walls and floor elevations need compiler-generated
entrance/approach paths. The compiler currently validates bounded types, IDs,
references and graph connectivity; it does not certify continuous traversal.
Both SDK settlement products are **experimental candidates**, not promoted world
releases. `qualify-world-sdk.sh` exits nonzero on this gate. The default Cascade
adapter only imports the original five site dimensions, and keeps generation-v1
terrain routes, which still pass their existing physical reachability tests.
