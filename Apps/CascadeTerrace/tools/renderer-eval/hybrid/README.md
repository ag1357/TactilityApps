This proving integration compares directly with the source at
`ca942d39f4d9677a3015739e013818598b820b41`. No Jet runtime is linked.

From the application directory:

```sh
./scripts/qualify-world-sdk.sh
python3 tools/renderer-eval/hybrid/run.py
ASAN_OPTIONS=detect_leaks=0 python3 tools/renderer-eval/hybrid/run.py --sanitize
```

LeakSanitizer is disabled only because the managed audit executor uses ptrace;
ASan bounds checks and UBSan remain enabled. No allocation leak result is claimed.
The SDK gate generates `build/macro.cws`. Run timing without concurrent builds.
Results go to `results/worldsdk/renderer-hybrid/` and contain source hashes,
compiler/host, all seven interleaved batch samples and individual-frame p95.
Sanitized timing is diagnostic only.

The candidate walks conservative incremental row spans and retains the original
coverage and reciprocal-depth expressions for every candidate pixel. Small
bounding boxes retain the old loop. The span bound includes a roundoff allowance
and two additional pixels; ill-conditioned edges widen/disable pruning. This is
not an unconditional solid-span fill and does not replace float vertices with
integer projection. No persistent renderer memory or scene interface is added.

The harness compares exact color, depth and write/triangle counters on 12,000
seeded primitive cases, two shared-edge cases, four game seeds across 12 yaw
angles, and all 22 macro modules across eight yaw angles. It captures clipped
triangles once for the four published benchmark views. Timing runs separate
translation units with no timers or capture hooks in the production/reference
renderer. Raster replay includes identical per-triangle dispatch and depth clear;
it is reported separately from complete scene rendering. Presentation is excluded.

A passing suite establishes bounded tested parity, not a proof for arbitrary
nonfinite or unbounded inputs. Retain the selectable reference for investigation:
`-DCT_RASTER_REFERENCE=1` on desktop, or `idf.py -DCT_RASTER_REFERENCE=ON reconfigure`
for P4. Set the option back to OFF before building the normal candidate package.
Physical P4 performance and display qualification remain pending.
