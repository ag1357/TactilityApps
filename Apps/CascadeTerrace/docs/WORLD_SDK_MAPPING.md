# World SDK Gate VI mapping sheet — v0.1

The content schema (`ANAPHORUM_WORLD_SYSTEMS_BIBLE_v0.1/WORLD_SDK_CONTENT_SCHEMA.md`)
requires Gate VI to publish **conceptual field → actual SDK field → unit →
version → migration → test** before any parity vectors are frozen. This is
that sheet, for the canonical event boundary and the epistemic social
projections. Every row cites the suite that proves it; every count is an
exact literal from the current qualification run. Nothing here reinterprets
a legacy quantity: the legacy Cascade game stays the gameplay authority and
its Hydro-chit economy keeps its meaning.

## Command, commit and event boundary

| Conceptual | Actual SDK field | Unit | Version | Migration | Test |
|---|---|---|---|---|---|
| EventId `(WorldId, epoch, sequence)` | `WsState.revision` (global commit watermark) + `WsFeed{epoch, revision, actor, target, kind}` / `WsState.tail` entries; world identity is the recipe CRC the state validates against | dimensionless counters, `uint32` | state wire v4 (social tail); op envelope unchanged since Gate F | schema-1 states without social records encode v1/v2/v3 byte-identically | `tests/world_state.c` (3,873), `tests/social_sdk.c` §wire, `tools/worldsdk/social_test.py` mirror_wire |
| Command (`command_id`, actor, request sequence, action, targets, expected revision, parameters) | `WsOperation{sequence, epoch, base_revision, action, target, amount, aux}` + `WsContext{player, at, server_authority}` | sequence per player; `amount` in item units of the target slot | op envelope v1 | unchanged | `tests/bridge_sdk.c` (163), `tools/worldsdk/network_test.py` receipt replay |
| Revision (monotonic, overflow explicit) | `WsEntity.revision` (entity-local), `WsState.revision` (global), `WsReceipt.base_revision` | dimensionless, `uint32`, wrap-safe comparisons | v1 | unchanged | `tests/social_sdk.c` §10/§11 (revision mismatch and stale reuse), `tests/bridge_sdk.c` |
| Retry returns the existing event/receipt | `WsReceipt{sequence, epoch, base_revision, revision, action, target, amount, aux, status, flags}` at `WsState.receipt[player]`; bit 0 rewarded, bit 1 world_changed, bit 2 historical | one receipt per player | wire v4 (28 B/player) | absent before v4 ⇒ states without receipts decode as before | `tools/worldsdk/network_test.py` reconnect+replay, `tools/worldsdk/social_test.py` retry/stale |
| One committed event, atomic mutations | `ws_apply` commit order: state mutation → `ws_record` tail/feed entry → `witness_project` evidence formation; all under one `WsState.revision` bump | one revision per commit | v4 | — | `tests/bridge_sdk.c` outcome preservation (legacy `State` memcmp identical every op), `tests/social_sdk.c` |
| Authority assigns deterministic order; no merge of exclusive ownership writes | `ws_merge` (unchanged Gate G semantics; deterministic, never AI-mediated) | — | v1 | unchanged | `tests/world_state.c` cases A–E |
| Private act is canonical but not public news | feed kind `WS_QUIET` (0xFFFF): tail entry only; SHOW/TELL/EXCHANGE commit without publishing | — | v4 | — | `tools/worldsdk/network_test.py` (feed untouched, revision +2), `tools/worldsdk/social_test.py` privacy cases |
| Institutional delivery is delayed, not instant | `WsPending{observer, subject, teller, root, deliver_s, ...}` + `WS_REPORT_DELAY_S` (3600 s) + `ws_file(clerk, observer, subject)` + `ws_social_settle` / `ws_clock_advance` | canonical world seconds | v4 | — | `tools/worldsdk/network_test.py` (filed at rev 7, delivered FACTION_NOTICE at rev 8 after clock +3600), `tools/worldsdk/social_test.py` delayed delivery (675, FACTION_NOTICE, replacement) |

## Evidence and projections

| Conceptual | Actual SDK field | Unit | Version | Migration | Test |
|---|---|---|---|---|---|
| EvidenceRecord (handle, proposition, observer, subject, teller, root, epistemic class, confidence, salience, clock, context, valence) | `WsEvidence{observer, subject, teller, root, kind, confidence, context, clock_s, salience, valence}`; handles: NPC = entity index+1 (1..WS_CAP), player = `WS_EV_PLAYER`(0x10000)+index; `root` = the committed revision | confidence/salience 0..1000 (thousandths); valence −1000..1000; `clock_s` seconds | wire v4 (34 B/record) | absent before v4 | `tests/social_sdk.c` (3,814), `tools/worldsdk/social_test.py` (20,465) |
| Epistemic class | `WS_EV_ACT/CLAIM/CONTRADICT/RESTITUTION/PROMISE/KEPT/BREACH/REPORT` | enum | v4 | — | both social suites |
| Claims are recorded, never verified as truth | `WS_TELL` records `WS_EV_CLAIM` with valence 0; view counts claims separately and no projection converts a claim into an act | — | v4 | — | `tools/worldsdk/social_test.py` claim-grade (375) + teller kept, `tests/social_sdk.c` |
| Hearsay degrades at formation and never rises through copying | teller ≠ 0 ⇒ confidence = direct × `WS_EV_REPORTED_SHARE`(3) / 4, capped `WS_EV_REPORTED_MAX`(750); teller 0 = direct observation outranks all copies | thousandths | v4 | — | `tools/worldsdk/social_test.py` §6.7 informs + delivery, `tests/social_sdk.c` |
| Corroboration counts the root once | `ws_evidence_put` replacement semantics on `(observer, root, teller, kind)`: the same cause retold replaces, never stacks | — | v4 | — | `tests/social_sdk.c` (kyra acts 7, trust 481: exchange record replaces the same-root projection), `tests/bridge_sdk.c` (acts 11, trust 887) |
| Time decay, pinned history | bucket = (`clock_s` − anchor) / `WS_EV_DECAY_S`(86400), weight = confidence >> bucket; bucket > 15 gone; `salience ≥ WS_EV_PINNED`(500) ⇒ bucket 0 forever | seconds; truncating integer division; arithmetic shift | v4 | — | `tools/worldsdk/social_test.py` decay clocks (pinned survives, minor gone), `tests/social_sdk.c` |
| PerceptionEdge (observer→subject conduct estimate, counts, confidence, watermark) | `ws_social_view` → `WsView{trust, acts, claims, contradictions, restitutions, promises, kept, breaches, reports, confidence, watermark}` — pure function of (records, clock), query frequency cannot forgive | trust −1000..1000; confidence capped 1000 | v4 | — | full 23×23 observer×subject sweep in both suites |
| RelationshipEdge is a distinct record | `WsRelationship{trust, reliability, cooperation, aggression, confidence, promise}` (player↔player, Gate F); affinity never grants capability | bounded `int16`/`uint16` | v1 | unchanged | `tests/world_state.c` |
| Contextual valence re-interpretation without rewriting history | `WS_CTX_ARENA/WS_CTX_DEFENSE` halve negative valence, `WS_CTX_RESTITUTION` doubles positive, capped ±1000, evaluated at view time (`effective`) | thousandths; trunc toward zero | v4 | — | `tests/social_sdk.c` §12 promise context (−110000 ctx), `tools/worldsdk/social_test.py` |
| Witness list is authority-assigned | `witness_project` over `ws_witness` (distance, scope, 128-sample occlusion): confidence = (30000 − manhattan)/30 at the station edge ⇒ 900; witnesses through doorways see interior acts | thousandths; mm manhattan | engine witness gate (Gate E, unchanged) | — | `tests/social_sdk.c` exact literals (940/790/790; interior 3+3+3=9; oren out of range; toma occluded), `tests/bridge_sdk.c` (900 at station edge) |
| Dialogue can cite an actual cause | `ws_social_cite` returns the strongest retained (root, kind, salience) or the honest empty limit | — | v4 | — | `tools/worldsdk/social_test.py` full cite sweep |
| NPC↔NPC and authority-mediated information | `ws_inform(observer, subject, teller, kind, confidence, context, valence, salience, root)` — same record machinery, root must reference a committed revision; self-report rejected | — | v4 | — | `tools/worldsdk/social_test.py` §6.7 dax/kyra/marisol/oren/clerk, `tests/social_sdk.c` |
| Uninformed observers stay uninformed | no `WsEvidence` rows: `ws_social_view` returns empty (C returns 1 with zeroed counts), no feed entry, no record | — | v4 | — | oren (out of range) and toma (occluded) rows in both suites |

## Bridge (legacy game → canonical boundary)

| Conceptual | Actual SDK field | Unit | Version | Migration | Test |
|---|---|---|---|---|---|
| The game is the gameplay authority; the boundary only observes | `content/cascade_adapter.h`: `Bridge{ws, sequence, station, market, kyra, field, last_promise}`, `bridge_init` (cast-checked), `bridge_apply(op, subject)` → `BridgeResult{status, d, canonical}` | — | Gate VI adapter API | new file; no legacy signature changed | `tests/bridge_sdk.c` (163): legacy `State` memcmp identical after every op through both paths |
| Bridge statuses | `BRIDGE_COMMITTED / DENIED / UNMAPPED / REFUSED` — the game refused, or committed with no canonical mapping, or the canonical layer refused (bounded resource) | — | — | — | `tests/bridge_sdk.c` |
| Action vocabulary mapping (bounded, honest) | REPAIR/DAMAGE→station(4); EXTRACT→waterfall(9); SHOW→`WS_SHOW` (slot_of bounded, IT_NOTE unmapped); TELL(subject handle)→`WS_TELL`; TRANSFER/GIVE/BUY/SELL-with-kyra→`WS_EXCHANGE`; PROMISE lifecycle→`WS_PROMISE`/`WS_KEEP_PROMISE` (promise_state 1→2/1→3, teller = player for speech, 0 for authority-observed facts); market trades (non-person counterparty), WAIT/CONDENSE/PICK_UP, notes stay `BRIDGE_UNMAPPED` | module indexes are product-local handles | — | — | `tests/bridge_sdk.c` |
| Neutral-chit economics rule (schema §Compatibility) | no reinterpretation: `WS_EXCHANGE` notarizes counts and counterparties only; the legacy Hydro-chit quantity, prices and inventory bytes are untouched; a neutral-chit ruleset is deferred to the economy gate | legacy chit units | — | no migration needed (nothing moved) | `tests/bridge_sdk.c` byte-identical legacy state; `./build/test` (864) green |

## Typed time, rounding and RNG (frozen for parity vectors)

- Canonical clock: `WsState.clock_s`, world seconds; advanced by the
  resource tick (schema-3 worlds) or `ws_clock_advance` (schema-1). No
  combat-time or accelerated-time conversion exists in this gate; adding
  one later requires a new schema version.
- Dispatch delay: `WS_REPORT_DELAY_S` = 3600 s. Decay window:
  `WS_EV_DECAY_S` = 86400 s.
- Rounding is frozen: truncating integer division for buckets and
  hearsay degradation (×3/4), arithmetic right shift for decay weights,
  trunc-toward-zero halving for arena/defense valence, capped doubling
  for restitution, and `trust += effective × weight / 1000` trunc-div.
- No new RNG domain: witness confidence, evidence formation, projections
  and delivery are deterministic functions of committed state; the
  weather RNG (Gate III) is unchanged and does not feed social state.

## Budgets and persistence

| Conceptual allowance | Actual bound (measured) |
|---|---|
| evidence handles | `WS_EV_CAP` 64 records × 34 B (wire) in `WsState.ev` |
| pending dispatches | `WS_PEND_CAP` 8 × 34 B |
| retry receipts | `WsReceipt` 28 B × `WS_PLAYER_CAP` |
| state footprint | `sizeof(WsState)` = 13,744 B (was 10,920 at Gate V: the fixed social arrays grow every state, used or not); the gate-fixture full wire is 4,404 B |
| feed | `WS_FEED_CAP` 16 public entries (unchanged) |

Persistence: `ws_save`/`ws_restore` carry the v4 section through the same
two-slot recovery; compaction and the conservation ledger identity are
enforced by `ws_state_validate` after every commit. `ws_state_hash` is
the CRC32 of the tail-less payload — the Python mirror is byte-identical
against the C encoder (payload, header and CRC), which is how the hash
convention was pinned.

## Preservation proof (schema §Compatibility)

- The Cascade product is unchanged: 1,200 bytes, same CRC
  (`tools/worldsdk/social_test.py` asserts `cascade_bytes` 1200); the v1
  save fixture was not regenerated (no recipe CRC change).
- Schema-1/2/3 state **wires** without social records are byte-identical to
  the published artifacts (the v4 section is conditional); the fixed-size
  state struct did grow from 10,920 to 13,744 B, which is in-memory only.
  `tests/world_state.c` passes 3,873 checks over the unchanged cases.
- Gate I–IV literals remain bit-identical (macro product 2,196 B,
  ruin→haven 2,668,102, gate toll 1,500, karst close between 80,000 and
  79,999); Gate V literals unchanged (36 slots, 8 species).
- The normal game passes its original suite: `./build/test` 864 checks,
  0 failures; the desktop qualification script is untouched.

## Evidence index (where each number lives)

| Gate | C proof gate | Python parity gate | Network |
|---|---|---|---|
| I traversal/adversarial | `results/worldsdk/traversal.json` (56) | `navigation_probe.py` 18/18, `adversarial_test.py` 15/15 | — |
| II macro geography | `results/worldsdk/macro.json` (4,419) | `macro_test.py` 9,995 | — |
| III resource/ecology | `results/worldsdk/resource.json` (322) | `resource_test.py` 1,114 | — |
| IV anomaly topology | `results/worldsdk/anomaly.json` (130) | `anomaly_test.py` 440 | macro scenario (in `network_test.py`) |
| V creatures/NPCs | `results/worldsdk/creature.json` (920) | `creature_test.py` 7,495 | — |
| VI events/witnesses/social | `results/worldsdk/social.json` (3,814) + `results/worldsdk/bridge.json` (163) | `social_test.py` 20,465 | `network_test.py` 2,385 incl. the social boundary phase |

Sanitizers: `scripts/sanitize-world-sdk.sh` — 10 C suites clean under
ASan/UBSan (`results/worldsdk/sanitizer.txt`). P4 builds (portable
default 189,396 B ELF, PIE and PIE+PPA experimental variants) recorded in
`results/worldsdk/renderer-hybrid/p4-presentation-*.log` and
`results/p4-build.json`; physical validation remains pending.
