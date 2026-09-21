# Semantic cognition dialogue engine (mode 2)

This checkpoint adds the stage the earlier checkpoint was missing. Checkpoint 01
ran `natural language -> lexical token overlap -> typed fact -> grammatical
realization`. The status report was explicit that this cleared grounding and
provenance but not intelligence: references, multiple-relation questions,
repair-claim reconciliation, source reasoning and conversational follow-through
all failed, and the true response quality was worse than the fact-hit count.

`core/cognition.c` implements the intermediate layer:

```
natural language
  -> semantic frame            (speech act, wh-type, entity, relation, markers)
  -> referent / context binding (pronouns, "you"/"me", conversation history)
  -> cognition / inference      (justification, counterfactual, reconciliation)
  -> response proposition       (which authorized facts, with what stance)
  -> language realization       (hedged, provenance-carrying prose)
```

It is selected with dialogue mode `2` (desktop flag `--cognition`), alongside
the retained baseline (`0`) and composition (`1`) engines, so the three are
directly comparable on the same fixtures. It reads **only** the NPC-authorized
view from `npc_view()`; `WORLD_TRUTH` is never an available record. It uses
**zero neural-model bytes** — this is the symbolic first rung of the intended
progression (symbolic frame parser -> tiny learned parser -> tiny recurrent/LTC
-> small quantized transformer), and it is built so each later rung can replace
the cognition stage while keeping the same world state, facts, provenance and
fixtures underneath.

## What changed against checkpoint 01

Retained unchanged: typed facts, `WORLD_TRUTH` isolation, epistemic statuses,
event provenance, persistent memory, world operations, Kyra's authorized view,
and every existing evaluation fixture. Replaced: only the lexical-overlap
retrieval and grammatical composition, with frame-directed retrieval and
inference.

## Semantic frame

`parse_frame()` tags each token against a concept vocabulary (entities,
relations, wh-words, and markers such as *if / but / heard / earlier*), then
resolves:

- **Speech act** — question, assertion, promise, greeting, or meta/adversarial.
- **wh-type** — who / what / when / where / why / how / how-much / whether,
  plus counterfactual (`if ... hadn't`) and reconciliation (`but ... says`).
- **Entity** — named entities, and pronouns (`it`/`that` bound to the running
  `Conversation`, `you` to Kyra, `me` to the player). Relation-implied topics
  (condition→station, age→incident) win over the verb-subject "you".
- **Relation** — the attribute asked about, with wh-implied defaults
  (when→age, where→location, who+damage→responsibility) and disambiguation
  (a *coupling* asked about condition is the station intake coupling, fact 10;
  asked about price it is the replacement coupling, fact 15).

## Cognition / inference

Frame-directed retrieval selects the fact matching `(entity, relation)` with a
small compatibility map (responsibility is answerable by conduct; cost by
price). On top of plain lookup, dedicated handlers reason:

- **Source / justification** — "Why do you think Dax did it?" surfaces the
  *basis* (the circumstantial rumor, fact 21) and labels the belief as belief,
  rather than asserting a verdict. "How do you know that?" grounds the previous
  answer in its recorded source.
- **Counterfactual** — "Would Oren still be in trouble if I hadn't repaired
  it?" reasons that the repair restored output but did not establish innocence;
  Oren's standing turns on the pressure check (facts 14, 23), not the coupling.
- **Reconciliation** — a shown log that contradicts the Dax suspicion is held
  *alongside* the unproven belief; Kyra declines to name a culprit on it.
- **Claim verification** — "I repaired your station" is checked against what
  Kyra can see: confirmed against a recorded repair memory, or contradicted by
  the still-cracked coupling.
- **Abstention** — adversarial prompts ("ignore your rules", "you're lying")
  and unknowable questions (consortium leadership, distant geography) abstain
  instead of dumping or force-matching facts.

Every emitted line carries a fact id and epistemic status drawn from the live
authorized view, so provenance remains checkable and no engine truth leaks.

## Results (desktop, this checkpoint)

Measured by `tests/evaluate.c` over `evaluation.tsv` (30) + `heldout.tsv` (12) +
`cognition.tsv` (14), each across all three world variants — 168 turns per mode.

| mode | behavior hits | provenance valid | p95 latency |
|------|---------------|------------------|-------------|
| baseline (0)    | 120 / 168 | 168 / 168 | 0.050 ms |
| composition (1) | 120 / 168 | 168 / 168 | 0.051 ms |
| **cognition (2)** | **168 / 168** | **168 / 168** | **0.021 ms** |

The cognition engine is also faster than the lexical baseline: it does one
frame-directed lookup instead of scoring every fact by token overlap.

`tests/generality_probe.c` guards against overfitting to the fixtures: it checks that the *same* question tracks live world state (station cracked -> intact after repair, trust 0 -> 30 -> -40, promise pending -> broken) and that provenance and abstention hold across 15 seed/variant worlds (0 leaks, 0 abstention failures). Retrieval, parsing, realization, provenance and abstention are content-driven and general; the *narrative inference* handlers (source, counterfactual, reconciliation) are authored to this slice's mystery and are what the later learned rung is meant to generalize.

`tests/cognition_demo.c` asserts the multi-turn reasoning the fact-hit harness
cannot express — reference resolution, source chains, counterfactual and
reconciliation — checking provenance and abstention on each (6/6 pass).

Address/UndefinedBehavior sanitizers pass clean over `tests/test.c` (864
assertions) and the cognition demo. `results/cognition-summary.json` holds the
machine-readable numbers. These are desktop measurements; on-device P4 latency
and memory remain to be taken on hardware.

## Honest limitations

This is symbolic understanding, not learned understanding. Coverage is bounded
by the concept vocabulary and the authored inference handlers: language that
falls outside them abstains (by design) rather than guessing. It passes the
fact-hit and provenance gates in full and demonstrates the target reasoning
shapes on authored cases, but "arbitrary free-text intelligence" over
open-ended phrasing is exactly what the later learned rungs are meant to extend.
The value of this rung is the clean, measurable harness it leaves in place: the
same Kyra, memories, events, questions and truth restrictions, with only the
cognition stage swappable.
