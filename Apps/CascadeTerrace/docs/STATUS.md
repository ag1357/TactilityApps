# Checkpoint 01 qualification status

**IMPLEMENTED / DESKTOP VERIFIED in part / P4 BUILD PRODUCED / Q1 INCOMPLETE / Q2 PENDING PHYSICAL VALIDATION**

This is a real playable checkpoint, not completion of the mandatory mission.

## Verified evidence

- Shared C runtime builds for SDL and the P4 external-app ABI.
- 100 seeds pass the implemented route, spawn, bridge, foundation and evidence checks.
- 864 core assertions pass at this checkpoint, including deterministic generation, fixed-point Phos against a numerical oracle, movement, epistemic isolation, observed evidence, variant preservation on reload, serialized round-trip, and recovery from truncated/corrupted newer save slots.
- Actual SDL causal replay: navigate, free-text interaction, claim, promise, accumulate/condense Phos, buy coupling, navigate back, repair, save, terminate the process, reload, retrieve the repair through different wording, unknown question, rumor question, alternate seed.
- Address/undefined-behavior sanitizers passed the preceding 855-assertion revision; LeakSanitizer is unavailable under the execution environment's tracing. The final revision needs the refreshed sanitizer run recorded alongside this file.
- Dialogue: 21/30 **minimum fact-hit checks**, in all three variants, for both modes. Paraphrases: 10/12 for each variant/mode. These are not full semantic acceptance scores. Composition provides no measured retrieval advantage and is not selected as the default.
- All emitted fact IDs/statuses matched the authorized NPC view in the evaluation harness. This does not demonstrate complete answers, correct relevance, natural phrasing or sound causal reasoning.
- Static P4 import audit found all imports in the inspected Tactility source export tables after removing an unavailable LVGL scaling call. Actual loader/module compatibility is pending hardware.

Desktop timings, exact binary sizes and hashes are in `results/desktop-performance.json`, `p4-build.json`, `p4-sections.txt`, and `summary.json`. They are not physical P4 measurements.

## Missing or inadequate requirements

1. Dialogue does not meet the intended free-text intelligence gate. It uses lexical slot overlap, weak context, and grammatical composition. No learned model was trained. References, multiple-relation questions, repair-claim reconciliation, source reasoning and conversational follow-through fail. The full response-quality judgment is worse than a simple fact-hit count.
2. World geometry does not yet implement genuine multi-level sector/portal topology. The tunnel is an enterable shell, not the required underground-to-rooftop route. The market is elevated but stacked navigable floors are missing. The lift is visible but not functional.
3. Mantle, arbitrary persistent drops, full market/consent behavior, mood/Guild trust, all personality effects, and all quest/evidence consequences are incomplete. NPC scheduling changes the materialized location rather than animating travel.
4. No chunk streaming, adaptive terrain LOD, sector/portal visibility, PPA path or audio. Distance culling and depth occlusion are implemented. Resident heightfield is bounded for this district; this is not a demonstrated streaming architecture.
5. Memory uses a 32-event NPC FIFO and 96-event world FIFO. Old consequential memories can be evicted. The format is bounded, but long-term event compaction is not complete. Multi-player relationships are represented by one keyed player record, not a tested multi-player implementation.
6. The P4 frontend has not been run on hardware. Touch feel, actual panel layout, CardKB2 integration, stack/heap peaks, load/unload lifecycle, telemetry retrieval and input parity remain unverified. Some desktop controls remain richer than the P4 controls.
7. The P4 qualification mode is an initial measurement harness, not the full automatic acceptance suite. A camera recording or physical screen-capture integration remains required.
8. Memory sizes are explicit structure/ELF-section accounting and requested PSRAM, not measured device peaks. Save recovery tests simulate file interruption/corruption, not SD power failure. Cross-architecture runtime parity is still pending even though both binaries compile.

## Measured memory representation and projections

One NPC/player memory record is 5,384 bytes on the desktop ABI, below 8 KiB. Linear projected storage is 538,400 bytes for 100, 5,384,000 for 1,000 and 53,840,000 for 10,000. These are storage projections, not resident allocations, NPC behavior benchmarks or a full game database size.

The game materialization is 35,320 bytes and the renderer 153,636 bytes on desktop. The NPC view workspace is 10,952 bytes. Persistence uses 32 KiB wire scratch plus two bounded state workspaces. File-library, SDL/LVGL and stack overhead are separate.

## Next engineering work

First implement proper sector floors/portals and finish traversal/operations. In parallel conceptually, replace lexical retrieval with a evaluated semantic frame interpreter and explicit referent/context binding. Preserve current typed facts, event provenance, save tests and rendering baseline. Do not scale the corpus or adopt AetherSparse based on association alone. Physical telemetry should then decide whether rasterization requires fixed-point projection/PPA/LOD changes.

The attached source specification is kept privately with the original working copy. The public implementation includes the evaluation fixtures needed to reproduce these findings.
