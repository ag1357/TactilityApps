> General cognition research continuation: [measured results, failures and build instructions](docs/GENERAL_COGNITION.md). Modes 2–4 are experimental; mode 0 remains default. Q1 is incomplete and physical qualification remains pending.

# Cascade Terrace — P4 RPG implementation

**Playable implementation checkpoint. Q1 is incomplete; Q2 has no physical results.**

A shared C runtime generates the valley, renders at 240×160, accepts movement and free text, applies inventory/repair/Phos operations, and saves sparse state and bounded NPC memories. Desktop uses SDL2. P4 uses Tactility's native external-app ABI and LVGL canvas. No cloud cognition.

## Desktop

On Debian/Ubuntu or the CM5:

```sh
sudo apt-get install build-essential libsdl2-dev python3
cd Apps/CascadeTerrace
make
./build/cascade
scripts/qualify-desktop.sh
```

WASD moves; arrows turn; Shift runs; Space jumps. E interacts with nearby Kyra or intake; T opens conversation; Enter submits; Escape closes it; PageDown pages a long reply. R repairs, M meditates, C condenses, B buys a coupling at the market, P picks up evidence, O shows it to Kyra, N waits one game hour. F5 saves; F9 reloads. The normal keyboard substitutes for CardKB2 on desktop. Saves use `cascade.save.0` and `.1` in the working directory.

`--seed N`, `--save PATH`, `--load`, `--script FILE`, `--headless`, and `--capture DIRECTORY` support automation. Headless still runs SDL and the same renderer. The harness walk commands generate actual movement input through collision; they do not teleport. Test-fixture positioning in `tests/evaluate.c` is separate and explicitly not gameplay evidence.

Default dialogue is the deterministic typed-fact baseline. `--experimental-composition` enables the grammatical realizer. It remains experimental: neither mode passes the complete dialogue gate. Both share retrieval and epistemic records.

## P4 build and install

Use ESP-IDF 6.1 and the Tactility 0.8.0-dev SDK matching the installed firmware. The inspected SDK index identifies commit `21b100b7891a34875c683f1fd52bf99bd7e075ad`. A directory named **TactilitySDK** is required because ESP-IDF derives the component name from the directory name.

```sh
. /path/to/esp-idf/export.sh
export TACTILITY_SDK_PATH=/path/to/TactilitySDK
scripts/build-p4.sh
scripts/deploy-cm5.sh P4_IP_ADDRESS
```

This builds a RISC-V external ELF and the repository's standard manifest-v0.3 USTAR `.app` package. It does not flash a replacement firmware. The external `elf` target is deliberate: Tactility's SDK documents that the standalone firmware link from `idf.py build` can fail even when the external app is valid.

A prebuilt package is included in `releases/checkpoint-01/`. The matching SDK/firmware remains a deployment requirement. On CM5, install dependencies for `../../tactility.py` (`requests`) and enable Tactility's development server on the device.

The P4 adapter uses explicit PSRAM for the game, low-resolution renderer and 480×320 output canvas. CPU nearest-neighbor upscale is implemented; PPA is not qualified. CardKB2 must appear as a Tactility hardware keyboard to feed the focused textarea; standalone CardKB2 I²C/UART discovery is not yet integrated here.

## Qualification

`results/summary.json` and `docs/STATUS.md` state actual results and missing work. To enable the current hardware telemetry mode:

```sh
python3 scripts/package-p4.py --qualification
CASCADE_QUALIFY=1 scripts/deploy-cm5.sh P4_IP_ADDRESS
```

Telemetry is appended to `qualification.jsonl` in Tactility's app user-data directory and printed to app stdout. Copy that file back to CM5, then run `python3 scripts/parse-device-log.py qualification.jsonl`. It reports measured samples and leaves missing gates pending. The current qualification mode covers boot, render samples, frame intervals, heap snapshots, save timing and dialogue latency, not the entire requested automated on-device suite.

For power-cut testing: first save and reload normally, then record a changed state and interrupt power during a later Save operation. Reboot and verify recovery to a complete old or new generation; retain the SD files and telemetry. Desktop truncated-write tests do not certify SD-controller durability.

## Architecture and assets

- `core/world.c`: seed/version/archetype materialization, heightfield, route carving, collision and validation.
- `core/state.c`: fixed-step input, 30× world time, fixed-point Phos, typed operations and event/memory records.
- `core/persistence.c`: sparse inventory differences and bounded histories, explicit little-endian wire format, CRC and two save generations.
- `core/dialogue.c`: authorized NPC view, lexical relation normalization/retrieval, provenance and two realization modes. Engine truth is not an available dialogue record.
- `core/render.c`: clipped depth-tested software triangles, fog/daylight, modular structures, waterfall, vegetation, animated characters and UI primitives.
- `assets/kyra.json` / `kyra.mesh`: replaceable low-poly rig; `scripts/generate_mesh.py` reproduces it. Other geometry is authored in the renderer and is not yet fully externalized.

No AetherSparse or AGI V3 components were adopted. The inspection was not a controlled benchmark of either system. This project remains GPLv3 under the parent Apps license; font data derives from DejaVu Sans Mono and its permissive font license.
