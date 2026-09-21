# Cascade Terrace general cognition experiment

**Research implemented; general free-text acceptance FAILED. Default dialogue remains mode 0. Q1 INCOMPLETE. Q2 PENDING PHYSICAL VALIDATION.**

Starting public commit: `feab7c0c161a0e8b8f3dedef6249a38b8f5b5f24`.
Research branch: `work/cascade-general-cognition`.
The local implementation checkpoint `22372fe` is identified in `research/results/freeze.json`; its exact frozen source/model/data hashes are the reproducibility authority. The publication commit is reported by the repository branch and mission response.

This mission implemented and measured three rungs. It did not produce a production-ready replacement for Kyra's language interface. The central result is that a small deterministic executor handles explicit typed graphs, while the tested 101K-parameter language front end does not generalize enough to justify adoption. High synthetic accuracy must not conceal the failure on actual game dialogue.

## Results and qualification

| Measurement | Existing modes 0/1 | Patch A / mode 2 | General B / mode 3 | Learned C / mode 4 |
|---|---:|---:|---:|---:|
| Cascade original + paraphrase + patch fixtures, 3 variants | 120/168 | 168/168 | 45/168 | 27/168 |
| Final arbitrary worlds, exact propositions and disposition | not measured | not comparable | 1312/1344 (97.62%) | 1184/1344 (88.10%) |
| Final arbitrary worlds, exact citation set | not measured | 353/1344 (26.26%) | included above | included above |
| Learned parameters | 0 | 0 | 0 | 101,294 |
| Quantized model bytes | 0 | 0 | 0 | 101,432 |
| Production promotion | retained default | ineligible: authored mystery | FAILED | FAILED |

Cascade scores are minimum fact-hit checks, not semantic success. A's arbitrary-world citation score is an upper bound: fixed surrounding prose may be unsupported even when citation IDs are legal. Its internal scenario ontology is not equivalent to the new schema; unavailable stage metrics are not invented.

B's final-world rumor class fails 0/32. C fails direct fact, rumor, multiple relation, contradiction and consequence classes 0/32 each under the challenge wording. This fails the per-class gate despite apparently attractive overall numbers. The oracle-frame executor scores 1344/1344 on the limited final graphs; that is conditional reasoning evidence, not language-understanding evidence.

All B/C emitted propositions in this synthetic evaluation pass the schema-specific provenance/verifier checks; no supplied private-truth canary appears. Correct epistemic labels do not make an irrelevant answer correct. See per-item frames, consulted IDs, legal masks, operations, values, statuses, text and error attribution in `research/results/final-traces.jsonl.gz`.

The original 864 core assertions and 100-seed checks pass again. The original SDL causal sequence runs through repair/save, process termination, reload and recall. Both experimental modes also retrieve the persisted repair in separate processes with `research/reload.ct`, but emit an extra legacy OTHER memory; this is a recorded failure, not a clean semantic recall PASS. The 1008 stress checks cover identity equivariance, independent binary structural-equation interventions, source independence and integer C/NumPy parity. The 24 C safety tests pass with AddressSanitizer/UBSan (LeakSanitizer disabled in this environment).

The current P4 external-app ELF is **177,056 bytes**; the default app package is **194,560 bytes**. This is a compiled artifact, not evidence of successful device loading or physical performance. Hashes are in `research/results/p4-build.json`.

## Architecture and authority boundary

`core/semantic.h` is independent of `Game`, people, places, items and mystery content. Its input is only an authorized `CgView`: stable entity IDs, relation enums, facts, sources, epistemic status, timestamp, explicit negation/exclusivity, and optional binary structural dependencies. WORLD_TRUTH is absent from this interface. The synthetic private canary is kept outside it.

The pipeline is language → typed frame → legal operation mask → authorized records → bounded operator execution → verified propositions → mechanical realization. No cloud or host cognition service exists. The same C source/model runs in SDL and the P4 external app.

Rung B uses a general lexical grammar for operator/relation cues and a data-driven name linker. It is explicitly a deterministic language baseline, not a claim that lexical retrieval is reasoning. Actual reasoning starts after the frame: contradiction checks compare exclusive values/negation at the same time; corroboration requires distinct sources; causal operations follow explicit dependencies; counterfactuals REMOVE an event or SET a binary variable and recompute its descendants. Unknown edges, unauthorized statuses, cycles and depth exhaustion abstain. Temporal lookup distinguishes prior and current values. Belief update proposes a labeled belief from corroboration and does not mutate canonical truth.

Limits are 64 facts, 32 entity labels, 64 steps, 8 propositions and depth 12. Binary rules are AND equations only. These are deliberate research limits, not a general causal-language model. COMPARE returns the compared records; it does not implement arbitrary numeric or qualitative comparison prose. A verifier checks record identity/status/value and recomputes causal step legality; the realizer cannot add a new entity or world fact.

Rung C is a **Jev-inspired typed decision model**, not Jev reproduction: 1536 hashed word/bigram features, a shared 64-wide pooled embedding, ReLU, and six independent linear heads for operation, relation, epistemic request, speech act, negation and intervention. It predicts no English responses. Authorized names are neutralized before model features; entity binding and discourse focus remain deterministic. A 12-byte explicit context holds the referent. There is no learned long-term recurrent state, learned span/argument pointer, secondary-relation head, or calibrated confidence. These omissions are measured limitations.

Training uses NumPy/SciPy, seed 72191, 160 training worlds/6720 queries, 32 validation worlds, 60 fixed epochs and validation-only epoch selection (epoch 2). Integer inference uses int8 weights and int32 accumulators; 2944 head MACs plus at most 10240 embedding additions per inference, excluding tokenization/linking/operators. C and an independent NumPy integer implementation agree on 168 validation queries. No 250K/1M sweep followed: the small model lost to the deterministic baseline and its residual errors do not justify blind scaling.

The game adapter (`core/general_dialogue.c`) only consumes `npc_view()`. It maps the legacy relation vocabulary and preserves fact/source/status handles. It neither reads hidden truth nor manufactures causal equations from English descriptions. This exposed a material gap: the game's legacy authorized view mostly contains prose strings, without complete entity aliases, causal structure, timestamps or source registry. Experimental modes cannot yet interpret the full existing language or commit new free-text promises/claims. Their failure is retained instead of adding mystery handlers.

## Patch audit and comparison

Both attached patches were read fully and applied unchanged in a detached worktree at the starting commit. The original source and claims are preserved under `research/rung_a`; their reported 168/168 fixture behavior and the supplied demonstration/regression were reproduced. `research/results/input-patches.json` records attachment SHA-256 values.

`patch-audit.json` contains overlapping line inventories: 47 scenario lexical lines, 60 entity-rule lines, 17 evaluation-sensitive lines, 130 string/prose lines and 14 mystery-inference indicators, plus manual branch findings. Important examples are station/incident pronoun defaults, coupling-only promises, Dax responsibility/source handlers, Oren/log reconciliation and a fixed repair-versus-blame counterfactual. Citation membership never proved those added prose conclusions. A test-only adapter supplies the same arbitrary authorized records to the unchanged patch for the world comparison.

AetherCore contributes architectural principles: typed workspace, legal masks, bounded operations, evidence handles and deterministic verifier acceptance. No historical controller code was imported. The model is a language/operation selector, not an autonomous long-horizon policy; legal rejection and clarification remain explicit.

AGI V3/LTC comparison was not implemented. Explicit context retains the current referent; the measured small-model failures are language/schema failures. There is no measurement supporting an LTC advantage and no AGI claim.

## Partitions and experimental discipline

`research/data/manifest.json` records world counts, world hashes, partition hashes and identity hashes. TRAIN/VALIDATION/TEST/FINAL contain 160/32/32/32 complete worlds. Names and IDs are disjoint; causal depths are 2/3/4/5. Generated worlds combine possessions, jobs, goals, claims, rumors, contradictory values, corroboration, promises, changes and causal chains. These are abstract semantic records, not a second game generator.

Final-world bytes were sealed before training. A pre-training generator revision corrected an ambiguous source-question label found in validation; the final examples were not inspected. Final was opened only after implementation/model/evaluator freeze. No final failure was used to change cognition, parameters or language rules. The surface challenge bank is shared between TEST and FINAL: this is a held-out-world test, **not an independent blind-language benchmark**. The finite authored template families make synthetic language scores optimistic. Original Cascade evaluation prompts were never training data.

The freeze gate requires ≥90% exact final answers, ≥80% in every query class, perfect measured provenance, no measured unsupported propositions/truth leaks, and no regression below the existing game baseline. The learned rung must also beat B. Both fail. No winner has been promoted.

The final runtime is frozen byte-for-byte. P4 compilation generates a copy of `semantic.c` with only two explicit `int`/`unsigned` casts for printf arguments because its int32_t typedef differs from desktop. The operators, model and control flow are unchanged. CMake records this mechanical portability adaptation. Runtime cross-architecture parity still requires hardware.

A post-freeze gameplay probe exposed a secondary-relation sentinel bug: CG_OTHER can also match OTHER legacy facts, yielding unrelated extra memories. It remains documented in the failed frozen experimental candidate. It was not silently fixed and re-scored on final data. Other limits include 95-character legacy object truncation and incomplete source naming. These explain why neither promising graph tests nor a successful build establish game acceptance.

## Retrieval/materialization comparison

An actual isolated AetherSparse component was tested from `ag1357/AetherSparse` commit `c3aa2ef61e6ae77a12063e47221c6e4decae3762`, `src/aethersparse/controller/semantic_address.py`. Only its annotation-only import was guarded by TYPE_CHECKING. The algorithms and Apache license are retained under `research/vendor`.

The test compares a streaming typed-record scan, a conventional exact index and the occurrence-backed address plane over identical 128-byte records, 100–100000 records, 20-record active sets, and 0/90/100% controlled application-cache hits. All materializations are byte-identical. The supplementary `retrieval_pipeline.py` audit feeds those benchmark records into the actual same C operator/verifier for all three methods and four store sizes (36 checks); outputs are identical. The hot-latency probe in the main benchmark uses a separate authorized validation-world view. All three achieved full candidate completeness on this exact-name workload. An ambiguous-alias check preserves two candidates with probability mass one rather than forcing top-1.

At 100000 records and an application-cache miss, measured desktop mean materialization was 17.224 ms scan, 0.018 ms index, and 0.042 ms occurrence plane. Bytes read were 12.8 MB, 2560, and 2560 respectively. Python index residents were approximately 503 KB for exact indexing and 2.85 MB for the occurrence plane plus its ID mapping; packed-size projections are separately labeled. Repeated hot cognition reads zero storage bytes. OS caches were not evicted, so these are **not SD cold-start timings**. The occurrence plane adds uncertainty metadata but provides no measured exact-lookup advantage here. This narrow negative result does not falsify AetherSparse's other retrieval mechanisms or cognition architecture.

## Build, run and reproduce

Desktop requires a C11 compiler, SDL2 development headers/libraries, Python 3, NumPy and SciPy for research only:

```sh
cd Apps/CascadeTerrace
bash research/unpack.sh
make all build/libsemantic.so build/semantic_test build/evaluate
./build/cascade                         # unchanged default
./build/cascade --patched-cognition     # scenario-specific research baseline
./build/cascade --general-cognition     # experimental deterministic engine
./build/cascade --learned-cognition     # experimental int8 model
bash research/run.sh
```

`SDL_VIDEODRIVER=dummy ./build/cascade --headless --script research/experimental.ct` supports autonomous interaction. Screenshots and logs are under `research/results`. The source build uses the existing assets; no unrelated artwork was regenerated.

Retrain only for a new experiment: `OPENBLAS_NUM_THREADS=1 python3 research/train.py`. That changes model hashes and invalidates the frozen final protocol. Do not treat the released final worlds as fresh held-out data for future selection. To reproduce the released frozen result only: unpack, build `libsemantic.so`, then `python3 research/evaluate.py final`; do not tune from it. The original evaluation scripts and per-item results are provided, not expected-response lookup tables used by runtime.

P4 requires ESP-IDF 6.1, the Tactility 0.8.0-dev SDK, and its RISC-V compiler:

```sh
source /path/to/esp-idf/export.sh
export TACTILITY_SDK_PATH=/path/to/TactilitySDK
scripts/build-p4.sh
CASCADE_MODE=3 CASCADE_QUALIFY=1 scripts/deploy-cm5.sh P4_HOST
```

For a prebuilt qualification run, clone this branch on the CM5, install Python `requests` if needed, enter `Apps/CascadeTerrace`, and run `CASCADE_MODE=3 CASCADE_QUALIFY=1 scripts/deploy-cm5.sh P4_HOST`. No compiler is needed for that path; `CASCADE_REBUILD=1` requests a source rebuild.

The CM5 runs the repository's existing Tactility install/run client against the development server; it does not run cognition for the P4. A stock prebuilt default-mode app and a mode-4 qualification app are in `research/releases`. Both are experimental research builds and keep the same package identity as Cascade Terrace. `CASCADE_MODE` selects 0–4. Qualification logs record mode, structure sizes, dialogue latency, response CRCs, frame timing and heap/PSRAM readings. Record those logs before drawing any hardware conclusion.

## Memory and pending physical checks

The desktop semantic view is 10384 bytes; result/trace 2672; frame 28; context 12. The legacy bridge holds an additional 10952-byte authorized view. Compiler stack estimates and model/code accounting are in the result files. Existing NPC persistence stays 5384 bytes; projected storage for 1/100/1000/10000 NPC records is 5384/538400/5384000/53840000 bytes, not resident allocations. Generated dialogue is never saved as canonical truth.

The external ELF includes all experimental modes and the model, so its total size is not the minimal cost of B alone. Its `.bss` is 123720 bytes and `.rodata` 111468 bytes; the model placement is controlled by the Tactility loader. Do not assume SD-backed zero-RAM model access. Explicit renderer/game buffers still use the existing external-memory allocation path. Source/ELF accounting is not device heap/stack telemetry.

Pending: native app loading, firmware/module compatibility, actual internal SRAM and PSRAM peaks/fragmentation, model placement, stack high-water marks, dialogue first-output/complete latency and rate, fixed-vector desktop/P4 runtime parity, SD materialization/save/power-loss behavior, frame timing, thermal/power behavior and physical input/display confirmation. No physical PASS is claimed.

Next engineering step: repair the canonical semantic adapter and the recorded sentinel bug in a new experiment; build an independently authored, world-disjoint language benchmark with distractors and genuine multi-turn argument binding; then compare a compact contextual span/argument encoder. Preserve the typed executor/verifier boundary. Do not scale this pooled classifier or promote the authored mystery patch.
