# World SDK checkpoint report

**First implementation gate PASSED: 18/18 declared walk edges traverse with
runtime ground and collision, and the 15 adversarial spatial cases pass with
impossible connections failing generation cleanly.** The SDK spatial/access
abstraction is repaired: topology generates access ports before geometry,
module `pos.y` is the walkable top surface, corridors resolve to the most
local path, and reachability is capability-aware. Default Cascade gameplay
remains on its validated legacy terrain routes; the SDK multiplayer fixture
has not replaced the normal game. Macro geography, resource/ecology, the
nonlocal Phos edge and physical P4 qualification remain pending.

| Area | Status | Evidence |
|---|---|---|
| Finite schema, 128-bit ancestry IDs, binary compiler | IMPLEMENTED | Two recipe products; unknown extensions rejected |
| Deterministic module generation | IMPLEMENTED | 2,000 seed/recipe cases; 4,402 malformed products rejected; host/C parity vectors |
| Continuous SDK walkability | IMPLEMENTED | 18/18 direct edges pass; 56 focused C checks incl. ports, capability reachability, direct walks |
| Adversarial spatial cases | IMPLEMENTED | 15/15 generated cases; slope/port/blockage/vendor impossibilities fail generation cleanly |
| Default game compatibility | IMPLEMENTED | 864 assertions, 100 seed validations; repair/save/process-exit/reload replay |
| Shared renderer/collision, scoped rooms/lift | IMPLEMENTED | Port-aware geometry shared by renderer and collision; two recipe viewers |
| Two rendered desktop clients | IMPLEMENTED | 120 frames each; peers rendered; independent positions; shared extraction/repair and equal public hashes |
| Server semantic authority | IMPLEMENTED | 48 protocol checks including transfers, replay rejection, reconnect/restart |
| Offline merge | PARTIAL | C cases A–E pass; server-retained-base network protocol not implemented |
| Save/checkpoint compaction | IMPLEMENTED | 3,767 authority/persistence checks; v1 fixture regenerated for the revised recipe; torn-slot recovery; mixed histories through 100,000 operations |
| Owned room, decorations, typed feed | PARTIAL | C persistence and server fixture; complete normal-game UI/visitation pending |
| Complete Cascade multiplayer quest | NOT_STARTED | Existing single-player arc preserved |
| NPC population/schedule/social/witness integration | NOT_STARTED | Partial types/helpers exist; no complete integrated subsystem |
| External P4 app build | IMPLEMENTED | Real 32-bit RISC-V external ELF built and packaged |
| Native SDK multiplayer on P4 | NOT_STARTED | Transport fixture runs on desktop |
| Physical P4 validation | PHYSICAL_VALIDATION_PENDING | No hardware result claimed |

The default game measured 1.641 ms mean desktop rendering in the final core run.
The two rendered network clients measured about 0.660 and 0.741 ms per frame in
a small station view. These are different workloads, not a speedup comparison.
They received 47,145 and 46,613 bytes over 120 frames, with semantic snapshots
omitted from unchanged movement responses. These are loopback measurements.

The recipe workspace is 10,276 bytes; semantic state is 9,280 bytes; renderer is
153,636 bytes. These are C object sizes, not hardware allocator measurements.
Cascade's recipe is 1,200 bytes. Fixed-population mixed histories reconstruct
exactly from approximately 1.8–1.9 KB of resolved state plus that immutable recipe,
with zero retained operation tail after compaction. Irreducible ownership,
inventories, reward epochs, relationships and recent public feed remain explicit.
Arbitrary unique data and unbounded populations are not compressed away.

The default P4 ELF is 181,152 bytes; package 194,560 bytes; `.text` 50,900 bytes;
`.rodata` 112,640 bytes; `.bss` 134,000 bytes. The immutable recipe adapter adds
10,280 bytes of static materialization/cache state. SDK functions not referenced
by normal gameplay are compiled but discarded from the packaged ELF; these sizes
are not a fully integrated SDK multiplayer client footprint. A future runtime
would additionally require its active semantic state, transport buffers and
persistence workspace. Actual RAM/PSRAM placement and latency remain unmeasured.

ASan/UBSan passed for SDK and state suites. LeakSanitizer cannot operate under this
executor's ptrace setup, so those runs used `detect_leaks=0`. New C files pass
strict warnings and formatting; Python passes Ruff F/E9 checks. The P4 SDK emits
its existing LVGL configuration pragma note but no compile error.

The recommendation is to continue from this foundation with the traversal gate
passed. Next gate is macro geography: express mountain/valley, plains,
river/watershed, lake or wetland, forest distribution, cave, ruin, two
settlements and a wilderness route between them through compact recipes and
seeded modifiers, proving the hierarchy without settlement-specific engine
logic. Then bridge the normal Cascade actions and native P4 transport to the
verified authority layer. Do not expand cognition research or claim physical
P4 results from desktop measurements.

Machine-readable results and traces: `results/worldsdk/report.json`,
`generation.json`, `navigation.json`, `adversarial.json`, `network.json`,
`client-1.json`, `client-2.json`, `compaction.json`, `state.json`, and
`p4-build.json`. Exact source hashes, release artifact hashes, build commands
and pending work are preserved in the P4 record and `FACTORY_CONTINUATION.md`.
