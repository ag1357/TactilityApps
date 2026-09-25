# Anaphorum VI-P2 — implementation and qualification report

2026-09-25. Branch: `work/anaphorum-vip2-platform-input`, continuing the existing mission from exact `eeb7de9fce00f15ebd2f14c07a388d6574fb7957`. This is the bounded platform/input/lifecycle gate. No multiplayer, renderer optimization campaign, new cognition, or new world canon was undertaken.

Implementation and host gates pass. A default P4 external app was built with ESP-IDF **v6.1 / 6.1.0** and an SDK generated from the upgraded Tactility fork. Physical Device A performance, background CPU, heap reclamation, and wired input remain unmeasured. The shared keypad ownership limitation and optional I2C controller limitation below remain qualification constraints.

## Published checkpoints

| Checkpoint | Commit | Scope |
|---|---|---|
| A | `b962a2a20e63f93d4d785f7724a6a92a25e93b1f` | Content-space viewport and exact inverse |
| B | `b5b9105887b02c18cbeb1cbdadaf5045686ef3e5` | Shared actions, saved bindings, capture and queued edges |
| C | `eecafdf035947fdb1b7f68a15fb7364ee4c076ab` | Tested grant/revoke/close lifecycle |
| D | `2be2155b26bbb1e9d150e73e1fbf71eb10b354bf` | Optional generic I2C adapters and protocol/platform tests |
| E | `e03ed7757bd974fe0ba041c65d464cc94b9e9870` | Nearby interaction identities and actual-motion actor pose |
| F | The commit containing this report | Integrated native/SDL frontends, regression evidence, default P4 package |

The separate Tactility SDK packaging correction is [dd18d0f](https://github.com/ag1357/Tactility/commit/dd18d0f0c2b871b21800eb172627c34c931d91d4), on `work/anaphorum-vip2-sdk-packaging`. It changes three build/test files. Firmware runtime, drivers, DTS, exports, scheduler, pins, C6 firmware, and eFuses were not modified or flashed.

## Layout and normal UI

The native frontend measures the window-create callback's supplied content root. System chrome remains outside that root. A single aspect-preserving viewport fits the bounded 160×240 internal image into the available content rectangle. RGB565 nearest-neighbor presentation with SDK-matched buffer alignment/stride and touch inversion use the same integer mapping. Resize/regrant rebuilds the layout; touches outside the viewport clear movement. Gameplay buttons and movement/look feedback are children of the viewport. Menu access stays accessible in the content root.

The permanent eleven-button debug bar is gone. Normal play offers movement/look feedback, Jump, Run, nearby contextual interaction, and Menu. Resume, Save, Controls and Quit are implemented. Save uses the existing local persistence path. Quit follows the app cleanup path and returns from the external application. Menus suspend local player input/simulation; they are not a future multiplayer global-pause protocol.

The SDL reference uses the same viewport/action/interaction code. [Seven host screenshots](vip2-desktop/README.md) show 320×440, 640×320, 480×480 and 960×640 layouts, Menu, Controls and a binding conflict. These are real SDL output, not P4 screenshots. The viewport test additionally covers offset content roots, portrait/landscape/square spaces, noninteger scales, tiny/empty areas, and 1,723,276 exact pixel/inverse comparisons.

## Input architecture and measurement

The native keyboard task drains actual kernel press/release queues independently of rendering, targeting 10 ms between acquisitions. Current fork BLE/USB keyboard drivers support this API. Per-device runtime instances retain held state; binding identities use backend/control plus a stable device identity. A disconnect reconciles only that source. Conversation transfers keyboard text input to LVGL; ownership reacquisition discards stale keyboard records whose releases LVGL may have consumed. Touch state is reconciled separately. Revoke/close clears pending gameplay input and stops further sampling/render work.

Digital presses/releases and menu-direction transitions are bounded counters, not a single frame latch. Analog actions include time-weighted values, a deadzone and neutral hysteresis. Short presses survive slow frames. Repeated keydown does not invent new edges, and there is no elapsed-time release heuristic. Fractional analog camera yaw is retained despite the legacy integer yaw field. Jump edges wait for a physics step.

The portable threaded test observed **12 acquisition ticks, mean 10.101 ms, minimum 10.079 ms, maximum 10.160 ms** during a 150 ms stalled render consumer. This is a host measurement, not a P4 claim. Native qualification JSONL records actual sampling count/mean/min/max, source identity/control/value, action axes/held/edges, queue overflow, render/presentation time, viewport size, helper join, and final heap counts. Enable with `--qualify` or the existing qualification marker. Normal play does not write qualification telemetry.

Optional I2C runs in its own worker because the generic controller's mutex acquisition can wait for another owner. It cannot stall the keyboard sampler. Transfers use a short timeout; controller references cover one poll only and are released before sleeps. The current controller mutex is not a hard bounded cancellation interface: a wedged competing bus owner can still delay that optional worker's join. Default configuration creates no I2C worker.

SDL consumes timestamped events on its main thread. Their timestamps and queued edges preserve short input across a render stall; it does not claim an independent SDL hardware sampling task.

## Bindings and Controls

Actions: MOVE_X, MOVE_Y, LOOK_X, LOOK_Y, JUMP, RUN, INTERACT, MENU, BACK, VIEW_TOGGLE. Defaults retain WASD, O/P, I and U. Space jumps; R runs on the native keyboard; Escape opens Menu and Backspace is Back. Arrow/Enter alternatives and touch bindings are supplied. Shift is available to backends that report it as a control (SDL does); native modifier-only events are not assumed.

Controls displays every action and its current bindings. Bind New accepts a digital key/button or either analog direction after held controls return neutral. Many controls may bind to an action. Conflicts require explicit replacement or cancellation; full/invalid binding attempts report failure. Existing Menu/Back controls cancel native capture, and Escape/Cancel remain available. Restore Defaults saves the reset configuration. CardKB2 press-only keys cannot bind movement axes or RUN.

Bindings are app-local `controls.cfg`, separate from the game save:

```text
ANAPHORUM_BINDINGS 1
backend stable_device control kind direction action sign
```

Each subsequent line contains seven numeric fields. Backend IDs: keyboard=1, touch=2, seesaw=3, CardKB2=4, SDL gamepad=5. Kind: digital=1, analog=2. Action IDs follow the list above, starting at zero. Device zero in a binding is a wildcard; runtime connection IDs are never persisted. Direction chooses a physical analog half-axis; sign chooses the action polarity. At most 96 bindings are accepted. Writes use a temporary file and rename; malformed/version-mismatched/conflicting loads leave current bindings intact. Native storage stays under `ag1357.cascadeterrace`; existing save bytes and `ws_*` vocabulary are unchanged.

## Hardware status and exact limits

| Backend | Implemented | Still required |
|---|---|---|
| Native BLE/USB keyboard | Actual per-device press/release sampling, chords, capture and disconnect reconciliation | Device A cadence and hotplug qualification |
| Seesaw 5743 | Product verification, real 10-bit axes/six-button states, no-IRQ polling, independent instances/addresses and mock tests | An existing configured external controller, verified wiring and physical test |
| CardKB2 I2C | ASCII FIFO pulses, discrete actions, W/S menu navigation and dialogue text; actual Enter `0x0a` normalized | Same external controller/wiring test; held/chord gameplay is not available from this protocol |
| SDL controllers | Stable GUID plus serial/path identities, analog/digital capture, source-specific disconnect | Actual controller qualification; a nonserial device path can change |

The upgraded fork DTS instantiates internal I2C on GPIO7/8; it does not instantiate the proposed external controller. Generic I2C transactions are already exported, so there is no missing transaction API to solve with a game-specific firmware hack. The app opens only explicit controller names in optional `controls-i2c.cfg`. GPIO4/5 are candidates identified by source/schematic review, not declared free by the app. UART2/3 stays reserved for AetherLink. No pins are automatically provisioned. See [the sourced I2C audit and configuration format](VI_P2_I2C.md).

The current SDK has no exported keyboard lease, prior-indev-enabled query, or Device-to-indev lookup. The foreground app therefore claims keypad streams and restores only tracked, still-live keypads on revoke. It never disables touch/pointer devices. However, it cannot preserve an independently pre-disabled keypad's prior enabled state. This is **not a proven multi-app input lease**. The supported qualification case is the fork's normal enabled BLE/USB keypad pipeline. Future push-only keyboard drivers also need generic exported subscription support. These limitations are recorded rather than bypassed with private LVGL structs or scheduler changes.

## Gameplay fixes and content boundary

INTERACT resolves actual nearby content by 3D range, existing traversal accessibility and deterministic distance/identity ordering. It returns the selected entity/affordance and carries that identity through conversation submission. No target opens no conversation; unknown NPCs never fall back to Kyra. Current Cascade content supplies Kyra, untaken evidence, station repair and market purchase affordances. Dialogue production remains mode zero. Entry/exit and submission range are checked.

Actor facing, actual collision-resolved movement and locomotion phase are transient fields distinct from saved camera yaw. Camera panning while idle does not walk or rotate the actor. Blocked movement stops walking; run changes the step rate; jump uses the airborne pose. Player geometry rotates with facing. Legacy save loading initializes transient facing without changing the wire format. The SDK-only viewer uses a neutral pose until it has a separate movement snapshot.

Portability audit: viewport, actions, lifecycle and generic interaction selection contain no device drivers or character IDs. Hardware adapters and LVGL/FreeRTOS ownership live in the native frontend. Cascade-specific target enumeration and Kyra dialogue eligibility live in `content/cascade_interaction.c`. Existing legacy geography/render dependencies remain visible; this gate did not undertake an unrelated engine rewrite. The sparse building/content gap is still present and recorded. River traversal retains the existing no-swimming behavior and authored crossings. No filler buildings, wading rules or new swimming canon were added; the declared traversal and adversarial crossing gates passed.

## Verification and exit evidence

- Existing Gates I–VI qualification: pass, including 18/18 navigation and 15/15 adversarial spatial cases.
- Legacy: 864 checks; 100 seeds; save remains 734 bytes; State remains 21,840 bytes; wire CRC32 remains 3349299003.
- VI-P2: viewport inverse, bindings/persistence/capture, multi-device/chords/disconnect/short edges, analog and navigation hysteresis, 51 gameplay checks, 74 lifecycle checks, I2C protocol and actual platform-reference tests: pass.
- Lifecycle tests use the production lifecycle module with a real threaded 150 ms render worker: revoke permits only an already in-flight completion, discards its stale presentation, blocks further frames, supports repeated regrant, and joins on background/in-flight close. Native callbacks use that module and wake shared event groups; helper joins precede shared resource release.
- Ten existing plus six VI-P2 ASan/UBSan suites: pass. LeakSanitizer is disabled because it cannot run under the workspace's ptrace environment; this is not a leak-free physical-device claim. The existing sanitizer script was fixed to apply its documented ASAN_OPTIONS to executed tests as well as compilation.
- SDL actual frontend self-test and causal before/after save fixtures: pass.
- Default P4 external-app cross-build: pass, exact ESP-IDF6.1/fork-generated SDK, matching silicon flags. Dynamic import audit: 133 imports, zero missing from pinned firmware export tables.
- Separate SDK packaging: four tests and 24 matching cross-compiled LVGL ABI/configuration values.

Evidence is in [results/vip2](../results/vip2), [P4 build hashes](../results/p4-build.json), [platform provenance](VI_P2_PLATFORM.md), and [host screenshots](vip2-desktop/README.md). The package is [releases/vip2/ag1357.cascadeterrace.app](../releases/vip2/ag1357.cascadeterrace.app). It was built and published, not deployed or flashed.

Reproduce from the app directory:

```sh
scripts/qualify-world-sdk.sh
scripts/qualify-vip2.sh
scripts/sanitize-world-sdk.sh
scripts/sanitize-vip2.sh
# Activate exact IDF6.1 and set TACTILITY_SDK_PATH as described in VI_P2_PLATFORM.md:
scripts/build-p4.sh
python scripts/audit-p4-imports.py --firmware /path/to/Tactility
```

Stop at VI-P2. Physical ownership/input/background/exit measurements and external-bus provisioning remain the next qualification work; they do not justify advancing to multiplayer or altering world canon.
