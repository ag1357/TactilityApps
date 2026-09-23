# Renderer evaluation: current core/render.c vs Jet-derived paths on ESP32-P4

Bounded evaluation ordered at fourth-gate acceptance. Three paths were
benchmarked on identical scene data before any production renderer change:

1. the current `core/render.c` (instrumented copy, semantics unchanged),
2. the real [CubeCoders/Jet](https://github.com/CubeCoders/Jet) renderer
   (MIT; clone + measurement-only patch) driven on the same triangles,
3. the best P4-specific composition: Jet techniques + PIE SIMD kernels +
   the PPA DMA engine, each piece verified against the production toolchain.

**Recommendation: HYBRIDIZE.** Keep the current renderer's architecture and
take Jet's span-rasterization technique for the inner loop, plus PIE/PPA for
presentation. Do not adopt Jet as a library. Details and numbers below; the
proving integration is a ~60-line inner-loop change in `core/render.c` plus a
presentation change in `main/main.c`, proposed only after this report.

All timings are from the bench host (aarch64, `-O2`) — the same host for every
path, so ratios carry; P4-specific effects (PSRAM bandwidth, PIE) are derived
separately and marked as such. Nothing below is speculated where it could be
measured: every number comes from `results/worldsdk/renderer-eval/`.

## Scenes

Identical data for every path: the cascade-district legacy game scene and
three macro `render_world` scenes from `build/macro.cws` (watershed vista,
cave interior, ruin closeup). The instrumented current renderer dumps its
world-space triangles + camera per scene; the Jet benchmark replays exactly
those dumps with the camera mapping derived in closed form
(`rot.x = -pitch`, `rot.y = +yaw`, `fovFactor = 145`, near 200 mm — this
reproduces the current transform term-for-term; the only constant difference
is the screen-y center, 78 vs 80, a 2 px bias).

## Path 1 — current renderer, stage times (ms/frame)

| scene | tris drawn | transform | clip | setup | rasterize | clear | total | px written | depth writes |
|---|---|---|---|---|---|---|---|---|---|
| cascade-district | 825 | 0.140 | 0.140 | 0.012 | 2.124 | 0.005 | 2.778 | 110,431 | 110,431 |
| macro-vista | 117 | 0.002 | 0.002 | 0.002 | 0.530 | 0.006 | 0.557 | 23,323 | 23,339 |
| macro-cave-interior | 133 | 0.002 | 0.002 | 0.002 | 0.762 | 0.005 | 0.781 | 28,169 | 28,193 |
| macro-ruin-closeup | 123 | 0.003 | 0.003 | 0.002 | 1.354 | 0.004 | 1.390 | 81,841 | 81,897 |

**Rasterization is 76-97% of attributed time.** Transform + clip + setup are
0.006-0.28 ms — never the bottleneck. Coverage tests touch 3.2-4.5x the
pixels actually written (358,470 tested vs 110,431 written on cascade):
the per-pixel edge-function recompute with no incremental stepping is the
cost being paid, exactly what Jet's `TriangleSpans` removes.

Presentation micro-benchmarks (host, per 480x320 frame): the current
per-pixel upscale loop **0.1718 ms**, u32-pair row duplication **0.0117 ms**
(15x faster), half-width expand **0.0059 ms**, 300 KB memcpy **0.0084 ms**.
The P4 frontend's `main/main.c` per-pixel C upscale is the slowest of all
measured presentation forms before even reaching P4-specific effects.

## Path 2 — Jet on the same data, stage times (ms/frame)

Three configs: `z` = z-buffer + full width (apples-to-apples with the
current per-pixel depth), `zh` = z-buffer + half-width buffers,
`p` = painter (64-bucket counting sort) + half width, no z-buffer.

| scene | current total | z total (clear/prep/raster) | zh total | p total (prep/raster) | tris (cur/jet) |
|---|---|---|---|---|---|
| cascade-district | 2.778 | 1.159 (0.015/0.401/0.743) | 0.899 | 1.218 (0.917/0.295) | 825/575 |
| macro-vista | 0.557 | 0.170 (0.012/0.010/0.148) | 0.098 | 0.046 (0.012/0.030) | 117/78 |
| macro-cave-interior | 0.781 | 0.226 (0.018/0.004/0.204) | 0.140 | 0.074 (0.018/0.056) | 133/102 |
| macro-ruin-closeup | 1.390 | 0.433 (0.012/0.012/0.409) | 0.232 | 0.081 (0.021/0.061) | 123/99 |

* Jet's raster core is **2.9-7.2x faster** than the current inner loop on the
  same triangles (cascade 2.124 -> 0.743 z / 0.492 zh / 0.295 p).
* Full-frame speedups: 2.3-2.6x (z), 3.1-6.0x (zh), 12-17x on macro scenes
  (p). The painter config loses on cascade only because its frontend sorts
  all 9,260 submitted triangles (0.917 ms prep) while the current float
  transform+clip of the same set costs 0.28 ms.
* Per-pixel-image agreement with the current renderer (pixel parity, best of
  +-2 px vertical alignment): cascade 57.9% (z) / **89.3% (p)**, vista ~45%,
  cave 83.4% (z) / 86.8% (p), closeup ~82%. Mismatch structure is understood
  (below); no gross error remains — fog-baked distant hills and near terrain
  match exactly, mid-frame detail differs.

### Fidelity findings (why parity is not 100%, and one real Jet bug)

1. **`colorBaked` aliases a shared static material** (Jet bug, found
   empirically): `Scene::emitTri` queues every baked-color triangle with a
   pointer to one shared `s_bakedMat` whose colour is overwritten at emit
   time, so all queued baked triangles rasterise with the *last-emitted*
   colour. Our harness avoids it with per-colour materials; any integration
   must do the same or patch upstream.
2. **Integer screen coordinates collapse sub-pixel triangles**: Jet projects
   to whole pixels, so distant/small triangles degenerate and are skipped
   (motes: 15 drawn vs 142 in the current; actors: 99 vs 176; vista loses 39
   of 117). The current renderer's float edge tests keep sub-pixel geometry
   visible. Phos motes are a first-class game element — this matters here.
3. **Straddling triangles with avgZ < near are dropped whole** instead of
   clipped (terrain 318 drawn vs 302 + ~150 clipped pieces in the current).
4. **FAST_Z is per-triangle depth**: with `z` the per-triangle average z
   decides overlap, which misorders large straddlers vs the current per-pixel
   z (why painter parity on cascade is 89.3% while FAST_Z is 57.9%).
5. S3-only asm (clear/fill/expand) and esp-dsp are cleanly compiled out on
   P4 — the portable C++ paths are what the numbers above measure.

## Path 3 — P4 PIE kernels + PPA

**PIE kernels** (`pie_kernels.S`, assembled with the exact production march
`rv32imafc_zicsr_zifencei_zaamo_zalrsc_zcb_zcmp_zcmt_xesploop_xespv`,
ESP-IDF 6.1 toolchain esp-15.2.0_20250929; disassembly in
`results/worldsdk/renderer-eval/pie-kernels.disasm.txt`):

* `pie_fill_span_u16`: RGB565 span fill — `esp.vldbc.16.ip` broadcast +
  `esp.vst.128.ip` fused store-increment, 8 pixels per store, hardware
  zero-overhead loop. 96 bytes of .text for both kernels combined.
* `pie_expand2x_row_u16`: scanout doubling — `esp.vld.128.ip` 8 pixels,
  `esp.vzip.16` self-zip (each pixel duplicated), two 128-bit stores =
  16 output pixels per 4-instruction iteration.
* PIE state is already handled: the app sdkconfig has
  `CONFIG_SOC_CPU_HAS_PIE=y` and the IDF 6.1 FreeRTOS RISC-V port has the
  PIE coprocessor save/restore machinery compiled in (lazy, dirty-bit
  tracked), so kernels are callable from tasks without extra setup.

**Cost model (P4, derived):** the 480x320 presentation writes 300 KB to
PSRAM per frame. At measured community PSRAM write bandwidth (~85-118
MiB/s) that is a **~2.5-3.5 ms bandwidth floor for any CPU path**; PIE
removes the instruction cost on top (the current per-pixel loop spends
~1.5M instructions ≈ 3.7 ms at 400 MHz *before* bandwidth; the PIE expand
is ~19.2K instructions ≈ 48 us of issue). PIE lane semantics of
`vzip.16` are verified against Espressif's documentation and the IDF's own
PIE test routines but not executed on hardware (none available).

**PPA** (`esp_driver_ppa`): `ppa_do_scale_rotate_mirror` with
`scale_x = scale_y = 2.0`, RGB565 in/out, PSRAM DMA capable, async with
done-callback. Espressif's own CI floor for SRM is 21M input pixels/s
(PSRAM, `ppa_srm_performance` in `components/esp_driver_ppa/test_apps`);
our operation moves 38.4K input pixels (~384 KB total traffic), i.e.
**~2-2.5 ms, fully off-CPU**. Cache-line alignment (L1+L2 for PSRAM) is the
only buffer constraint — `heap_caps_aligned_alloc` satisfies it.

## RAM / PSRAM / buffer budget (240x160)

| path | framebuffer | depth | internal SRAM | notes |
|---|---|---|---|---|
| current | 76.8 KB (PSRAM) | 76.8 KB (PSRAM) | 0 | + embedded mesh assets (~150 KB struct, unchanged) |
| Jet `z` | 76.8 KB | 76.8 KB | +18.0 KB bss (TrigLUT sin/tan tables) | |
| Jet `zh` | 38.4 KB | 38.4 KB | +18.0 KB | |
| Jet `p` | 38.4 KB | none | +18.0 KB | field/interlace buffers double the fb |
| hybrid (proposed) | 76.8 KB (or 38.4 for macro) | 76.8 KB (or none, module-banded painter) | +96 B (PIE kernels) | no LUTs, no C++ runtime |

Jet's render queue is a heap-grown vector (no compile-time cap; cascade peak
575 queued triangles is tens of KB worst case).

## Binary impact (P4 cross-compile, -Os, --gc-sections, production march)

| what | .text | .bss |
|---|---|---|
| current renderer (`core/render.c`) | ~1.5 KB | 0 |
| Jet `z` full reachable surface (incl. pulled libstdc++) | 94.1 KB | 18.0 KB |
| Jet `zh` | 89.7 KB | 18.0 KB |
| Jet `p` | 92.3 KB | 18.0 KB |
| PIE kernels (both) | 96 B | 0 |

An empty C++ surface links to 42 bytes under the same flags, so the Jet
figures are true incremental costs for this C application (the ELF budget
headroom is ~14 KB of the current 186,392-byte binary before the next
partition step; a 90 KB+ C++ runtime addition does not fit without a
partition change).

## Triangle / scene limits and chunk visibility

* Current: no compile-time triangle cap; 117-825 tris/frame across the four
  scenes; the P4 frame budget is the only limit.
* Jet: no compile-time cap either (heap-grown queue, fixed 64 sort buckets);
  object-level 8-corner AABB frustum cull + distance fade bands per object.
* Chunk/sector visibility: the SDK's module visibility rules
  (`ws_route_cost_state`, gate-open topology, fidelity bands) live in the
  frontend in both cases and are untouched by any path. Jet's per-object
  cull maps one `Object` per module/chunk naturally, and its parallel-band
  rasterization (painter config) matches the P4's two HP cores if that ever
  becomes useful. The hybrid keeps the current fidelity-band loop exactly as
  it is.

## Recommendation: HYBRIDIZE

**KEEP** the current renderer's architecture: C, float transform/clip
(measured 0.006-0.28 ms — not a bottleneck), fog model, module fidelity
bands, PSRAM policy, and the proven World SDK interface. **KEEP** the
per-pixel z-buffer semantics (per-pixel depth is what makes straddling
geometry and motes render correctly).

**TAKE from Jet** (techniques, not the library — everything listed is
compiled-out-able):

1. Span rasterization for `raster()`'s inner loop: incremental edge
   stepping with solved row ranges + paired-u32 span fill, replacing the
   per-pixel edge-function recompute. Expected 2.9-4x on the raster stage
   (the measured `z`/`zh` deltas) while keeping float vertices and the
   current coverage rule, so sub-pixel geometry and motes do not regress.
2. Half-width framebuffer (38.4 KB) as an option for the macro scenes'
   240-column output.
3. Painter bucket sort per module band only if the z-buffer is ever dropped
   (measured: painter's raster core is fastest, but its frontend loses on
   triangle-heavy scenes and per-pixel depth is worth keeping here).

**TAKE from P4**: the PIE span-fill kernel inside the rasterizer's span fill
and the PIE 2x expand (or async PPA SRM) for `main/main.c`'s presentation,
replacing the per-pixel upscale loop (host-measured 15x slower than paired
stores; P4-estimated 6-8 ms -> ~3 ms bandwidth-bound CPU, or ~2-2.5 ms
off-CPU via PPA).

**DO NOT adopt Jet as a library**: +90-94 KB .text and +18 KB internal SRAM
in a C app with ~14 KB ELF headroom; sub-pixel triangle loss (motes are
first-class content); avgZ straddler drops; integer vertex projection; the
`colorBaked` shared-material bug; and a slower frontend on triangle-heavy
scenes. Jet's sophistication (textures, lighting, postFX, sprites, field
buffers) is exactly the part this product does not need — which is also why
taking just the rasterization technique costs ~1-2 KB instead of 90.

### Proposed proving integration (pending approval — no production change made)

1. `core/render.c`: replace `raster()`'s per-pixel loop with an
   incremental-edge span fill (float vertices kept; same pixel-center
   coverage rule; same z-write semantics). ~60 lines, no interface change.
2. `main/main.c`: replace the per-pixel upscale with the PIE
   `expand2x_row_u16` kernel (or PPA SRM as a build option).
3. Rerun the full world-SDK qualification, the P4 rebuild, and this
   benchmark; confirm the raster stage speedup and pixel parity > 99%
   against the current output (the hybrid keeps the current rasterizer's
   semantics, so parity must be near-exact, unlike Jet's 45-89%).

## Evidence index

* `results/worldsdk/renderer-eval/current.json` — path-1 stage times, traffic
  counters, presentation micro-benchmarks.
* `results/worldsdk/renderer-eval/jet_{z,zh,p}.json` — path-2 per-scene
  stage times, triangle/pixel counts, parity.
* `results/worldsdk/renderer-eval/pie-kernels.disasm.txt` — PIE kernel
  encodings from the production toolchain.
* `results/worldsdk/renderer-eval/p4-sizes.txt` — P4 cross-compile sizes.
* `results/worldsdk/renderer-eval/*.png` — current vs Jet-z output per scene
  and the group-paint probe.
* `tools/renderer-eval/` — the full harness (instrumented renderer copy,
  drivers, Jet configs, PIE kernels, clone patch) with a reproduction
  README.
