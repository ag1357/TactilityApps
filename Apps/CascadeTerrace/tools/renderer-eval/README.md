# Renderer evaluation harness

Bounded, measured comparison of three rendering paths for the 240x160 -> 480x320
PS1-era pipeline on ESP32-P4, produced for the fourth-gate renderer evaluation
(see `docs/RENDERER_EVALUATION.md` for results and the recommendation).

* **Path 1 — current renderer**: `render_instr.c` is `core/render.c` with
  semantics unchanged, instrumented with per-stage timers
  (`bench_timers.h`), pixel/depth traffic counters, and a world-space
  triangle + camera dump so the Jet path replays identical geometry.
  `bench_current.c` drives it on four representative scenes (the cascade
  district legacy game scene + three macro `render_world` scenes from
  `build/macro.cws`) plus presentation micro-benchmarks.
* **Path 2 — Jet (CubeCoders/Jet)**: `bench_jet.cpp` drives the real Jet
  renderer on those dumps through its public pipeline API
  (`prepareFrame` / `rasterizeBand`), with the camera mapping derived
  exactly (rot.x = -pitch, rot.y = +yaw, fovFactor 145, near 200 mm) and
  per-triangle colors baked to match the current pipeline's fog/shade.
  Three configs: `cfg_z` (z-buffer, full width — apples-to-apples),
  `cfg_zh` (z-buffer, half width), `cfg_p` (painter + half width).
  `bench_groups.cpp` / `bench_paint.cpp` are isolation probes used while
  debugging replay fidelity (per-group draw counts, group-coloured paint).
* **Path 3 — P4 PIE + PPA**: `pie_kernels.S` holds the two PIE kernels
  (RGB565 span fill via `esp.vldbc.16.ip` + `esp.vst.128.ip`, and the 2x
  scanout expand via `esp.vzip.16`), assembled with the exact production
  march (`rv32imafc_..._xespv`). `p4_driver.cpp` is the synthetic app
  surface used to measure Jet's P4 binary size under `--gc-sections`.

## Reproducing

Run everything from the app root (`Apps/CascadeTerrace`); the cascade scene
loads its mesh asset via a path relative to the working directory.

```sh
# path 1 (host): instrumented current renderer + app core + world SDK
cc -O2 -I tools/renderer-eval -I core -I world -I content -o /tmp/bench_current \
   tools/renderer-eval/bench_current.c tools/renderer-eval/render_instr.c \
   core/state.c core/world.c content/cascade_adapter.c \
   world/geometry.c world/recipe.c world/resource.c world/state.c \
   world/traversal.c world/persistence.c world/schema.c -lm
/tmp/bench_current    # writes /tmp/renderer-eval/out/current.json + dumps + PPMs

# path 2: clone Jet (github.com/CubeCoders/Jet, MIT), copy src/* to jet-src/,
# apply jet-counter-patch.diff (counters + span-write counting only)
for cfg in z zh p; do
  g++ -O2 -std=c++17 -Ijet-src -Icfg_$cfg -DCFG_NAME="\"$cfg\"" bench_jet.cpp \
      $(ls jet-src/*.cpp | grep -v -E "Sample|Example") -o bench_jet_$cfg -lm
  ./bench_jet_$cfg   # writes /tmp/renderer-eval/out/jet_$cfg.json + PPMs
done

# path 3 (ESP-IDF 6.1 toolchain, esp-15.2.0_20250929)
riscv32-esp-elf-gcc -march=rv32imafc_zicsr_zifencei_zaamo_zalrsc_zcb_zcmp_zcmt_xesploop_xespv \
    -mabi=ilp32f -mno-cm-popret -mno-cm-push-reverse -c pie_kernels.S -o pie_kernels.o
riscv32-esp-elf-objdump -d pie_kernels.o
```

The Jet clone patch is measurement-only (global `jet_px_written` /
`jet_tris_drawn` counters, span-write counting on the half-width paths, and a
`JET_DEBUG_TRIS` env-gated projection print used during fidelity debugging).
No Jet behaviour is changed.
