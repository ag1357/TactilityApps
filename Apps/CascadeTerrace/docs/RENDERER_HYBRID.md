Stage A is promoted on desktop correctness/performance evidence and a real P4
external-app build. P4 runtime/display qualification remains PENDING; Q1 is not
promoted and Q2 remains pending physical measurement.

Starting checkpoint: ca942d39f4d9677a3015739e013818598b820b41.
First fix: e835061 restores save-slot header parsing and starts its regression
with clean slots. The full World SDK qualification passes after that correction.

The default raster now traverses conservative incremental row spans. It retains
float projection, existing clipping, exact original coverage expressions and
per-pixel reciprocal-depth testing. Small bounding boxes retain the original
path. There is no Jet dependency, solid-span depth shortcut, scene-interface
change, additional framebuffer, persistent state or PSRAM allocation.

Measured x86_64 desktop results, seven interleaved 100-frame batches:

| Scene | Raster replay speedup | Full renderer speedup | Frame p95 ms, reference → candidate |
| --- | ---: | ---: | ---: |
| cascade | 1.26× | 1.20× | 1.661 → 1.381 |
| vista | 1.97× | 1.97× | 0.391 → 0.192 |
| cave | 2.05× | 1.98× | 0.566 → 0.272 |
| closeup | 1.89× | 1.92× | 0.879 → 0.464 |

Raster replay includes identical depth clears and dispatch. Full frame here is
scene rendering, without presentation or a display. No claim of 2.9–4× is made.
The harness times uninstrumented production and pinned-reference translation
units. Exact color, depth and counter parity passes 12,000 generated primitive
cases, two shared-edge cases and 224 game/world camera cases. Tested parity is
100%; this is bounded evidence, not a universal float-equivalence proof.
ASan and UBSan pass; LeakSanitizer is disabled because this executor uses ptrace.

The full World SDK script passes, including 864 legacy checks / 100 seeds,
18/18 navigation, 15/15 adversarial cases, persistence, macro geography,
resources and anomaly topology. Machine-readable qualification records and
raw logs accompany this report.

Same toolchain / same SDK external ELF:
- Reference raster: 186392 bytes.
- Candidate raster: 187376 bytes (delta +984 bytes).
- Package: 204800 bytes. Repository ceiling remains 4 MiB for the ELF.
- Renderer record: 153,636 bytes, unchanged.
- No added heap allocation. Three temporary row bounds are 36 bytes; compiler
  stack-usage records include all scalar temporaries/call frames separately.
  Bounds helper uses 192 bytes and raster uses 208 bytes in the P4 build;
  nested call frames must be included in high-water qualification.

Built with ESP-IDF f21b4c238152dc9e3a24fbad9afe33a3d15f6cfd,
esp-15.2.0_20250929, TactilitySDK 0.8.0-dev. Exact ELF hashes, object sections and
compiler stack reports are in results/worldsdk/renderer-hybrid/. Compressed
Stage A ELF is retained in releases/worldsdk/renderer-hybrid/.

Presentation is a separate promotion. Stage A keeps the existing presentation
path. Correcting and qualifying PIE, or adding optional bilinear PPA, cannot
retroactively establish P4 raster timing. The prior evaluation's uninitialized
q1 kernel, out-of-bounds memcpy benchmark, FAST_Z equivalence claim and 14 KB
headroom claim are not accepted as proving evidence for this implementation.

Reproduce with tools/renderer-eval/hybrid/run.py and its README. P4 reference
fallback is selectable with CT_RASTER_REFERENCE; normal packaging uses OFF.
