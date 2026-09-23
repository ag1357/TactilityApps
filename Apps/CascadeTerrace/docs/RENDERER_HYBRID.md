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


Stage B: selectable presentation, with portable promotion

Stage A was independently published as 8bbe12ed2b1f793f93aa1e78c35c2015fc674935.
Its table above records that run; the current JSON includes a later combined
render-plus-presentation run. Variance between runs is retained, not hidden.

The release default is portable exact nearest-neighbor expansion. It preserves
all RGB565 pixels, uses paired alias-safe stores, adds no framebuffer and needs
no new firmware driver exports. Corrected PIE is selectable when compiled in.
PPA bilinear SRM is separately selectable and remains experimental. Neither
PIE execution nor PPA execution/latency/visual quality is physically qualified.
The quoted old 2–2.5 ms PPA estimate is not promoted into a measurement.

Portable presentation passes 9,395 assertions, including all 65,536 RGB565
values, odd widths, naturally aligned two-byte-offset buffers and canaries.
Async ownership passes 1,000 simulated completion cycles; these are host state
machine tests, not a PPA driver/hardware simulation. ASan and UBSan pass.
The historical presentation harness's buffer overread, machine-specific path
and aliasing writes are repaired; its corrected run passes ASan/UBSan. The old
results are retained and marked historical rather than silently replaced.

Portable expansion mean: 0.056995 → 0.028658 ms on the host.
Source is 76,800 bytes; scanout is 307,200 bytes. Directly measured combined
scene render + 2× expansion, excluding OS/display work:

| Scene | Reference → candidate mean-of-batch median ms | Speedup | Total p95 ms |
| --- | ---: | ---: | ---: |
| cascade | 1.5523 → 1.1827 | 1.31× | 2.0736 → 1.3610 |
| vista | 0.3376 → 0.1733 | 1.95× | 0.3993 → 0.2308 |
| cave | 0.4585 → 0.2443 | 1.88× | 0.5443 → 0.3586 |
| closeup | 0.7603 → 0.4093 | 1.86× | 0.8767 → 0.4978 |

The PPA ownership sequence is explicit: copy the renderer pixels into a private
snapshot; submit snapshot → back buffer; keep both immutable/owned while busy;
the ISR performs one release-store completion signal; the app polls under the
LVGL lock, swaps front/back, updates the canvas pointer and may submit again.
Frames arriving while busy are dropped for presentation; rendering/gameplay can
continue. Close drains accepted work before unregistering and freeing private
buffers. No forced timeout frees a DMA-owned buffer. Driver rejection uses the
portable path without publishing the unfinished back buffer.

This uses one additional 76,800-byte source snapshot and one 307,200-byte output
buffer: 384,000 extra PSRAM bytes, plus unmeasured driver allocations. The copy
also adds 153,600 bytes of read/write traffic per submission; capacity alone is
not the performance question. Both PPA output pointer and size are cache-line
aligned, and input/output are disjoint. Output is bilinear, so exact parity with
nearest-neighbor is not claimed. Callback dispatch, display ownership, PSRAM/LCD
contention, visual quality and close/drain behavior need on-device qualification.
Completion polling adds frame-loop latency. Async makes work overlappable; it
does not remove transfer wall time. CPU availability during async is unmeasured.

The PPA ELF imports ppa_register_client, ppa_client_register_event_callbacks,
ppa_unregister_client and ppa_do_scale_rotate_mirror. These are additional to the
known SDK imports. Firmware exports have not been verified, so this build is NOT
load-qualified. Do not deploy that ELF as the default. Header availability and
successful external-ELF linking do not demonstrate loader compatibility.

P4 external-app builds from the same source/toolchain:

| Build | ELF bytes | Extra frame storage | Runtime status |
| --- | ---: | ---: | --- |
| Portable default | 188100 | 0 | Physical validation pending |
| PIE compiled, PPA off | 188328 | 0 | PIE execution pending |
| PIE and PPA compiled | 189616 | 384000 when PPA active | Firmware exports and physical validation pending |

The portable default is +1,708 ELF bytes relative to the 186,392-byte original
reference, including Stage A (+984) and presentation/telemetry (+724). The package
remains 204,800 bytes. All build hashes, logs, source hashes, disassembly and the
PPA import gate are machine-readable in results/worldsdk/renderer-hybrid/.
The default installable package is releases/worldsdk/renderer-hybrid/portable.app.
The optional PPA ELF is clearly marked experimental and must pass the export gate.

The effective production march ends in xespv2p1 for the configured silicon
revision. compile_commands contains an earlier xespv flag too; the last flag wins.
Both flags are recorded. Corrected kernel disassembly uses the effective march.
Assembly and instruction-spec inspection do not replace lane/loop/alignment tests
on the actual board revision or context-switch testing.

Build/selection (after normal ESP-IDF/SDK environment setup):

```sh
# Default release
idf.py -B build-p4-native -DCT_RASTER_REFERENCE=OFF -DCT_PRESENT_PIE=OFF -DCT_PRESENT_PPA=OFF reconfigure elf
python3 scripts/package-p4.py
# Optional builds; hardware/firmware gate applies
idf.py -B build-p4-native -DCT_PRESENT_PIE=ON -DCT_PRESENT_PPA=OFF reconfigure elf
# Launch with --presentation=pie; otherwise portable remains selected.
idf.py -B build-p4-native -DCT_PRESENT_PIE=ON -DCT_PRESENT_PPA=ON reconfigure elf
# Launch with --presentation=ppa only after verifying the firmware exports.
```

`--qualify` logs the selected backend, submission/poll CPU time, in-flight state,
render time, total app work and frame period separately. Those fields are not
hardware transfer latency or measured CPU idle time; collect those with on-device
profiling. Unavailable backend requests fall back to portable. No cloud or PC
service is involved in gameplay.

Sources: [Espressif PPA API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/ppa.html)
and the pinned ESP-IDF driver/instruction sources. Q1 remains INCOMPLETE; Q2 and
all physical promotion requirements remain pending. PPA cannot block the already
published raster improvement or the portable crisp presentation path.
