# VI-P2 platform and SDK audit

Audit date: 2026-09-25. These are source inspection and Workspace build results, not measurements on Device A.

## Pinned source and toolchain

The upgraded fork is `ag1357/Tactility`, `work/waveshare-p4-audio-exports`, runtime commit `e423c281ca56914599f291c06f9ab7a3fe71d0c8`. It includes the live upstream main observed during this audit, `86010186d88952fb8c4d9e66747e39446d90fd11`, through merge `8a55ea3edc260f998364362d37f5bca013e452b9`. The fork's default `main` and `work/waveshare-p4-refresh` are older and were not used as the SDK authority.

ESP-IDF tag `v6.1` resolves to `fff9895c82d744c7237be8847347bdd1b07c6643`, reporting version 6.1.0. The compiler is `riscv32-esp-elf-gcc`, crosstool-NG `esp-15.2.0_20251204`, GCC 15.2.0. The previously available `v6.1-dev` checkout was not used. Its similarly named SDK also had unverified provenance and differing headers.

The app's final P4 build selects `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and `CONFIG_ESP32P4_REV_MIN_100=y`, matching the upgraded fork's Device A silicon policy. Its compiler ABI/architecture flags match the SDK firmware build exactly: `-mabi=ilp32f -march=rv32imafc_zicsr_zifencei_zaamo_zalrsc_xesploop_xespv2p1 -mtune=esp-base`. This corrects the previous app build's default revision-3 target.

The selected Waveshare ESP32-P4 board configuration was used to compile all fourteen archives packaged by the fork's ESP32 SDK generator. All 1,501 Ninja build steps passed. No firmware image was flashed; producing SDK archives does not require flashing or connecting the board.

## SDK packaging defect and reusable correction

The pinned firmware builds managed LVGL **9.3.0**, but the original `release-sdk-esp32.py` copied headers from the separate `Libraries/lvgl` submodule, which is **9.4.0**. It also copied `lv_conf_kconfig.h` to `lv_conf.h` without preserving the firmware's LVGL configuration. App preprocessing therefore used different defaults, including draw-buffer alignment 4 instead of 64 and the builtin allocator instead of the firmware's custom allocator. The compiler's `Possible failure to include lv_conf.h` message was evidence of this packaging problem, not an app layout problem.

The separate reusable Tactility branch `work/anaphorum-vip2-sdk-packaging`, commit `dd18d0f0c2b871b21800eb172627c34c931d91d4`, fixes only SDK packaging:

- Read the actual LVGL source directory and archive from ESP-IDF's build description.
- Package those matching headers, removing stale headers left by an earlier SDK generation.
- Generate a LVGL-only configuration snapshot from the firmware build; clear disabled LVGL Kconfig switches as well as supplying enabled values.
- Propagate that snapshot through LVGL's supported `LV_CONF_KCONFIG_EXTERNAL_INCLUDE` setting in the SDK CMake target.
- Reject a missing archive or unsupported custom LVGL configuration instead of silently producing mismatched headers.

Four regression tests pass, including a C-preprocessor check that conflicting application settings cannot change the packaged firmware LVGL settings, while unrelated application settings remain untouched. A separate cross-compiled probe compared 24 ABI/configuration values between the firmware compilation environment and generated SDK. Every value matched, including LVGL 9.3.0, RGB565 depth 16, alignment 64, custom allocator 255, image descriptor size 28, and event/style/flag values used by the app. This is bounded ABI evidence, not an exhaustive proof for every LVGL public type.

The runtime firmware source and its compiled archives remain unchanged by this correction. No game-specific firmware exports, scheduler modifications, GPIO changes, or calibration changes were introduced.

```json
{
  "runtime_source": "e423c281ca56914599f291c06f9ab7a3fe71d0c8",
  "sdk_packaging_source": "dd18d0f0c2b871b21800eb172627c34c931d91d4",
  "upstream_main_at_audit": "86010186d88952fb8c4d9e66747e39446d90fd11",
  "idf_tag": "v6.1",
  "idf_commit": "fff9895c82d744c7237be8847347bdd1b07c6643",
  "idf_version": "6.1.0",
  "compiler": "esp-15.2.0_20251204",
  "p4_minimum_revision": 100,
  "compiler_target_flags_match_firmware": true,
  "sdk_version": "0.8.0-dev",
  "lvgl_version": "9.3.0",
  "lvgl_config_sha256": "5e9dea16f3c467b28b1e55b3eb8e450a10fb393d2eb3ca3bb285b3294ab01e1e",
  "lvgl_header_sha256": "5b4b12f5629ce90d96e84fa71f9f7b7c0d90265d08e7aab4c9099a2a3e664e13",
  "kernel_archive_sha256": "40fae40e130f03615e6d2b60fc21eade59120b43281db946f0d72718ef76b813",
  "lvgl_archive_sha256": "65048c082a213a631d16e5f265acf10e1cc18c37c64351b061fb4a156eeb4e01",
  "sdk_archives_built": 14,
  "packaging_tests_passed": 4,
  "crosscompiled_abi_values_equal": 24,
  "firmware_flashed": false
}
```

To reproduce, check out the packaging branch, initialize its submodules, activate ESP-IDF v6.1 with the P4 compiler, and run from the Tactility repository:

```sh
python device.py waveshare-esp32-p4-wifi6-touch-lcd-35
idf.py -B build -DIDF_TARGET=esp32p4 reconfigure
python -m unittest discover -s Buildscripts/tests -v
```

Build the SDK archives without flashing:

```python
from pathlib import Path
import subprocess
modules = Path("Buildscripts/release-sdk-modules.txt").read_text().split()
names = ["TactilityKernel", "lvgl__lvgl", "minitar", "minmea"] + modules
subprocess.run(["ninja", "-C", "build", "-j4"] +
               [f"esp-idf/{name}/lib{name}.a" for name in names], check=True)
```

Then run `ESP_IDF_VERSION=6.1 python Buildscripts/release-sdk-esp32.py release/TactilitySDK`. Set `TACTILITY_SDK_PATH` to that exact `TactilitySDK` directory and run the app's `scripts/build-p4.sh`. Reconfigure the app after replacing an older SDK so the new target configuration propagates. The generated archives belong to build output, not the RPG source repository.

## Window and lifecycle contract

The authoritative implementation is `Modules/lvgl-window-manager-module/source/window_manager.cpp` and its public `window_manager.h`.

`window_manager_create_ext(app_id, create_widgets, destroy_widgets, user_data)` supplies a new container occupying the configured **content widget**, not the raw display. The system constructs the status bar outside this app container. Only the top window has live widgets: covering it destroys its widgets and revealing it calls `create_widgets` again. Cached widget pointers must be nulled in the destroy callback and rebuilt on grant.

Both callbacks execute with the LVGL lock and window-manager lifecycle mutex already held. They must not call window-manager create/remove/state-management operations or acquire application locks. A callback may set atomic visibility state and signal a preclaimed `TaskEventGroup` bit: exported `task_event_group_signal` calls the event-group primitive without taking the group's bit-allocation mutex. Claim the bit before window creation and release it only after window removal.

Application events contain `APP_EVENT_RESULT` and `APP_EVENT_CLOSE`; there is no window-grant event in `AppEvent`. `window_manager_get_state(id)` reports grant/revoke. `window_manager_await_state_change(id, timeout)` blocks only while the window is granted and returns immediately when already revoked, so it must not be used as a background polling loop. The app's callback signal combined with its close-event subscription permits a real blocking background wait.

Window revocation should stop new expensive frames, reconcile held state and relinquish input, and let any already-started bounded work finish. Final cleanup must join helper threads before closing telemetry, freeing shared buffers, or unloading the external ELF. Runtime background CPU and PSRAM reclamation still require Device A instrumentation; compilation alone cannot establish those measurements.

## Keyboard and input ownership boundary

`KeyboardKeyData` contains a Unicode key, `pressed`, `continue_reading`, Ctrl/Alt, and optional HID usage/modifier fields. Both current BLE and USB keyboard drivers create `KEYBOARD_TYPE` devices with bounded queues drained by `keyboard_read_key`. Their current paths therefore support an independent app sampler; no key-release timeout is justified.

`device_for_each_of_type` holds the device ledger lock throughout its callback. A device pointer must not be retained for later access without a reference. `device_get`/`device_put` protect short operations; readiness must still be checked explicitly. A stored binding identity should describe the backend/control and stable device properties, never a raw pointer or the order in which devices happened to connect.

The pinned export tables expose `keyboard_read_key` and `KEYBOARD_TYPE`, plus ordinary device discovery/reference functions. They do **not** export `keyboard_subscribe`, `keyboard_unsubscribe`, `keyboard_poll`, `lvgl_keyboard_find_by_device`, or `lv_indev_get_driver_data`. Header availability or successful static SDK linking does not make those symbols available to the external-app loader. Future push-only keyboard backends need a reusable export/ownership improvement.

The current SDK also lacks an exported prior-enable-state query or input lease. A foreground app that disables all keypad indevs cannot distinguish a previously disabled keypad when restoring them. This is an ownership limitation requiring an explicit resolution or qualification restriction; it must not be reported as a proven safe multi-app lease. The bounded workaround must never disable pointers or touch devices, and it must stop claiming streams immediately on revocation. The sampler/action engine's per-device state does not by itself solve this shared LVGL ownership limitation.

External ELF instance data is separate: `app_esp32_loader_service.cpp` allocates a new `Esp32AppRuntime`, loads/relocates its own `esp_elf_t`, and frees it on unload. There is no shared mapped-image cache in that loader. LVGL input-device state remains shared between instances despite their separate C globals.

## I2C boundary

Generic `I2C_CONTROLLER_TYPE`, read/write/write-read/register operations and address probing are already exported in `TactilityKernel/source/symbols.c`. A normal external app can discover and reference an already configured named controller. There is no missing generic transaction export requiring game-specific firmware changes.

This does not authorize an app to choose GPIOs or create an external bus over pins with unknown electrical/runtime ownership. See [VI_P2_I2C.md](VI_P2_I2C.md) for device protocols, pin audit, optional controller configuration, and the exact hardware qualification boundary.

## Dynamic import audit

An ELF can link against SDK archives yet still import symbols absent from firmware's registered module tables. The audit reads its `.dynsym` undefined names and compares against `DEFINE_MODULE_SYMBOL`, `DEFINE_MODULE_SYMBOL_ALIAS`, and `DEFINE_MODULE_SYMBOL_SIGNATURE` entries in the pinned firmware. FreeRTOS imports must be checked in `Modules/freertos-module/source/module.cpp`, not assumed available merely because ESP-IDF declares them. This table-membership test does not prove which modules a different installed firmware activates.

The initial VI-P2 build exposed `lv_obj_get_content_coords`, which was not exported. The app was changed to compose the supported coordinate/style APIs. The corrected-SDK build was checked with the following result; rerun the audit after any further rebuild changes this ELF hash.

```json
{
  "elf_sha256": "7e7130f4676fe7beabc0514a303246184f1f2a6460805a3f12521eca8c9e076b",
  "elf_bytes": 214472,
  "import_count": 133,
  "missing_from_pinned_export_tables": [],
  "free_rtos_imports": [
    "xQueueSemaphoreTake",
    "xQueueGenericSend",
    "xQueueCreateMutex"
  ],
  "scope": "source-table membership; physical loader and runtime modules remain unmeasured"
}
```

The FreeRTOS names above are all registered by `Modules/freertos-module/source/module.cpp`. The corrected SDK's LVGL compile definition was also confirmed in the app's generated compilation commands.
