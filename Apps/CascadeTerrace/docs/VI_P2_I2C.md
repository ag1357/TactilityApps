# VI-P2 optional wired input

## 2026-10-07 native verification

On the pinned `d9f7434e` firmware, `i2c-external` is provisioned on SDA GPIO4 /
SCL GPIO5 at 100 kHz. The user reported both pads and CardKB2 connected,
with pad 2's address trace cut. The former one-pad app configuration was backed
up and expanded to the three entries below. Real device protocol polls verified
both seesaw products at **0x50 / 0x51** and a successful CardKB2 FIFO read at
**0x5f**. Final short runs showed mask 7, 15–16 polls, zero failures, and
successful detection again after P4 reboot. Source identities are distinct.

This proves protocol readiness, not physical button/typing behavior or
electrical reliability. CardKB2 specifies 5 V power input; successful reads
with the reported 3.3 V supply do not prove reliable power operation. Do not
raise P4 signal voltage. No wire, firmware, pin assignment or address EEPROM
was changed by the agent.

The worker publishes atomic readiness for app UI policy. CardKB2 characters
already route directly to conversation; the textarea remains blurred/ungrouped
while that adapter is ready, avoiding a software keyboard triggered by the
kernel ledger's lack of an app-owned device. Disconnect enables ordinary
intentional focus on the next tap. Real LVGL host tests cover the policy;
native synthetic callbacks verify the wired branch. Actual keys/touch still
need the small hands-on UI check. Evidence: `results/ui-recovery/native-qualification.json`.

## Implemented boundary

`core/i2c_controls.c` implements a portable, mock-tested transport adapter for
Adafruit product 5743 and CardKB2. `main/i2c_input.c` supplies ordinary exported
Tactility I2C transactions. No firmware, pin assignment, C6 program, eFuse,
address EEPROM, or global settings are changed. A missing configuration means
disabled; a missing peripheral never prevents application launch.

There is **no missing generic I2C transaction export** in the examined fork
`ag1357/Tactility`, `work/waveshare-p4-audio-exports`,
`e423c281ca56914599f291c06f9ab7a3fe71d0c8`. Its
`TactilityKernel/source/symbols.c` exports the controller type and read/write
APIs; device lookup and reference management are exported too. The app obtains
a named existing controller, checks its type/readiness, and balances every
acquired reference at the end of that poll, including failures. No controller
reference is retained while the worker sleeps. It does not enumerate arbitrary
I2C addresses.

## Optional configuration

Place `controls-i2c.cfg` beside the application's other user-data files:

```text
ANAPHORUM_I2C 1
seesaw i2c-external 0x50
seesaw i2c-external 0x51
cardkb2 i2c-external 0x5f
```

The controller above was illustrative in the initial audit; the pinned board
now instantiates it as described above. Up to four lines/devices are supported. Each line selects
its own controller, allowing separate buses. Blank lines and whole-line `#`
comments after the header are accepted. Invalid versions, unknown backend
names, duplicate bus/address pairs, out-of-range addresses and more than four
devices reject the whole file. CardKB2 is restricted to its documented 0x5f.

Bindings use backend plus a nonzero FNV-1a 32-bit hash of controller name,
address and backend type. They survive configuration-line reordering; renaming
the controller or changing an address creates a different source identity.
The hash is not a hardware serial number and, like any 32-bit hash, has a
theoretical collision risk. Runtime instances use reserved IDs
`0x40000000 + configuration index`; those IDs are never persisted.

Use Controls to bind axes and buttons. No assumption assigns the first board
to movement or the second to camera. Seesaw controls are analog 0/X and 1/Y,
digital 0/A, 1/B, 2/X, 3/Y, 4/Select and 5/Start. Both analog polarities may be
bound. The shared action layer supplies deadzone/hysteresis, capture and
per-source reconciliation. CardKB2 controls are character codes.

## Seesaw 5743 protocol

Adafruit documents address 0x50 by default and 0x51–0x53 through the two address
jumpers. Two factory-address devices cannot share one bus without changing one
address. This gate never writes the address EEPROM or changes hardware jumpers.
Buttons use pins A=5, B=1, X=6, Y=2, Select=0 and Start=16, active low. Axes use
analog pins 14 and 15 and ten-bit values. The adapter uses Adafruit's inverted
axis convention; binding direction determines gameplay polarity.

On first successful contact, the adapter checks product ID 5743 in the upper
half of the version register, then configures the six inputs with pull-ups.
Register reads write the two-byte module/function selector with STOP, wait
500 microseconds, then read. They do not substitute an immediate repeated
START. Version is module/function 0x00/0x02; X/Y are 0x09/0x15 and 0x09/0x16;
GPIO state is 0x01/0x04. Values and masks are big endian. The adapter validates
both axes before publishing a complete snapshot. No IRQ wire is required or
enabled. A transaction failure disconnects only that source; retry rechecks
identity and initialization.

Primary references:

- [Adafruit product pinout](https://learn.adafruit.com/gamepad-qt/pinouts)
- [Adafruit official gamepad example](https://learn.adafruit.com/gamepad-qt/arduino)
- [Adafruit Seesaw implementation, examined revision](https://github.com/adafruit/Adafruit_Seesaw/blob/985b41efae3d9a8cba12a7b4d9ff0d226f9e0759/Adafruit_seesaw.cpp)
- [Seesaw register definitions](https://github.com/adafruit/Adafruit_Seesaw/blob/985b41efae3d9a8cba12a7b4d9ff0d226f9e0759/Adafruit_seesaw.h)

## CardKB2 semantics and coexistence

CardKB2 I2C mode (`Fn + Sym + 1`) exposes address 0x5f at documented 100 kHz.
Each direct one-byte read dequeues one ASCII character; zero means the event
FIFO is empty. It is **not a physical key-up report**. The firmware adds
characters on press/repeat, and provides no I2C held-key bitmap. Its 32-entry
ring buffer has one unused slot. Accordingly the adapter emits a discrete
press/release pulse at one timestamp. The action layer rejects axis/RUN
bindings and capture from this backend. Discrete menu, interact, view and jump
events remain useful; full sustained movement/chord parity is not claimed.
No release timeout is invented.

The manual's firmware-version section calls 0xf1 a communication address;
the firmware source clarifies that it is a **register selector written to
0x5f**, not a valid seven-bit slave address. This adapter only reads the key
FIFO and does not write the nearby manufacturing-test selector 0xf0.

UART has genuine key-index press/release frames and could support sustained
controls through another adapter. It is researched, not implemented here;
the existing GPIO2/3 AetherLink allocation is retained. USB serial output is
likewise not integrated. The I2C adapter emits raw character pulses which the
frontend can route to conversation text entry and menu navigation. The examined
firmware sends Enter as 0x0a, Backspace as 0x08, and `Fn + 1` as Escape 0x1b.
Its `Fn + D/Z/X/C` arrow handlers only emit in BLE HID mode; they return without
an I2C character. There are no 0xb4–0xb7 arrow bytes in this firmware. I2C menu
navigation therefore uses ordinary `w`/`s` characters rather than assuming the
older CardKB special-key protocol. Uppercase/symbol characters remain literal
text; backend bindings retain their actual byte identity.

CardKB2 0x5f does not conflict with seesaw 0x50–0x53. They can logically share a
100 kHz bus with appropriate electrical wiring. CardKB2 specifies a 5 V power
input while the seesaw supply should match host logic; do not treat a shared
power conductor as proven compatible. Verify a common ground, 3.3 V signal
levels, pull-ups and the physical cable pinout before connection. No bench
electrical validation was possible in this workspace.

Primary references:

- [CardKB2 product, modes and connector](https://docs.m5stack.com/en/unit/Unit_CardKB2)
- [Official user manual and protocol](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1225/Unit_CardKB2_User_Manual_EN.pdf)
- [M5Stack API explicitly distinguishes press-only I2C](https://m5stack.github.io/M5Unit-KEYBOARD/classm5_1_1unit_1_1_unit_card_k_b2.html)
- [Official firmware FIFO and register implementation](https://github.com/m5stack/M5Unit-CardKB2-UserDemo/blob/3f58674ccc70ee09ca969de563bdbc4efb0c6316/src/main/bsp/bsp_i2c.c)
- [Official firmware event generation](https://github.com/m5stack/M5Unit-CardKB2-UserDemo/blob/3f58674ccc70ee09ca969de563bdbc4efb0c6316/src/main/app/app_factory.c)

## Board ownership audit and exact remaining blocker

**Update — 2026-09-28: the external controller is provisioned.** Fork branch
`work/waveshare-p4-external-i2c`, commit `d9f7434e` (on runtime `e423c281` +
SDK packaging `dd18d0f`), provisions a generic external I2C controller as a
board capability: the second P4 I2C controller (`I2C_NUM_1`), SDA **GPIO4** /
SCL **GPIO5** per the official schematic's expansion-header nets (no other
claimant), initial frequency **100 kHz**, no IRQ, no eFuse, no permanent
configuration, no pull-up flags (the attached device supplies 3.3 V-referenced
pull-ups). The device name is the stable generic identity **`i2c-external`**,
opened by external apps through the existing exported I2C-controller API
(`device_get_by_name`). Internal I2C0 (GPIO7/8), AetherLink UART (GPIO2/3)
and the C6/ESP-Hosted transport are untouched. All fork host tests were green
before flashing; the flashed firmware's
`Tactility.elf` sha256 is
`21e0ee6e81b9becc0102957293ad4a23d38132931919f07f071c26e57a9e8596`.
On Device A the app's optional worker opened the controller by name
(`i2c_config configured:1 worker:1`) and the first physical 5743 at `0x50`
passed product detection. With an unpowered pad the external bus showed
`clear bus failed`/`reset hardware failed` (SDA held low through the STEMMA
pull-up rail) and recovered without reboot once the pad was powered; see
`results/vip2/device-a-i2c-onepad.txt`. The dual-pad arrangement (a second
5743 at `0x51`) remains config-only.

The historical audit (pre-provisioning) is retained below.

The examined fork DTS at the revision above declares:

| Function | Owned P4 pins / bus |
| --- | --- |
| Internal I2C0, 400 kHz | SDA7/SCL8; AXP2101 0x34, ES8311 0x18, touch 0x38 |
| AetherLink UART1 | TX2/RX3 |
| Codec I2S0 | MCLK13/BCLK12/WS10/out9/in11 |
| Speaker amplifier enable | 53 |
| Touch reset/interrupt | 29/50 |
| Display SPI/backlight | 20/21/23/26/27/28 |
| MicroSD | 39–44 |

The official single-sheet schematic exposes GPIO4/5 and the other reported
candidate pins on the expansion interface; the 4/5 nets connect to the P4 and
header without an additional function shown. GPIO4/5 do not appear as claimed
peripherals in this DTS. That supports them as **candidates**, not a claim
about the running physical device's ownership. The ESP-Hosted C6 and its
transport remain untouched.

The missing piece is a board-provisioned external controller, preferably HP
I2C1 on independently verified GPIO4/5 at 100 kHz for mixed CardKB2/seesaw use.
The existing generic ESP32 driver acquires GPIO descriptors with
`GPIO_OWNER_PERIPHERAL` and fails on an ownership conflict. Provisioning must
use that mechanism and verify live ownership; the game must not silently
create the controller. No such board change or firmware flash is included in
VI-P2. In particular the app does not reduce the internal 400 kHz bus speed.

- [Exact fork board DTS](https://github.com/ag1357/Tactility/blob/e423c281ca56914599f291c06f9ab7a3fe71d0c8/Devices/waveshare-esp32-p4-wifi6-touch-lcd-35/waveshare%2Cesp32-p4-wifi6-touch-lcd-35.dts)
- [Official board schematic](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-3.5/blob/main/schematic/ESP32-P4-WIFI6-Touch-LCD-3.5-schematic.pdf)
- [Generic driver and descriptor acquisition](https://github.com/ag1357/Tactility/blob/e423c281ca56914599f291c06f9ab7a3fe71d0c8/Platforms/platform-esp32/source/drivers/esp32_i2c_master.cpp)

## Sampling, synchronization and validation

The platform poll function selects one configured device per invocation in
round-robin order. Thus two devices each receive every second worker period;
the configured count and measured worker timing determine the actual cadence.
Transport errors back off that device for one second without removing others.
Transactions request a five-millisecond timeout rounded up to one OS tick.
There are three 500-microsecond preparation waits per normal seesaw snapshot.
Callbacks occur after I/O; they take only the input-state lock in the caller.

The fork's controller driver uses an unbounded mutex acquisition before its
timed transfer. Therefore its transfer timeout alone cannot prove a hard
sampling deadline under arbitrary bus contention. Optional I2C work runs
independently from keyboard acquisition. Physical cadence, disconnect
behavior under electrical faults, and two actual pads are not measured here.

`tests/i2c_controls.c` covers exact initialization/read selectors and mask,
axis endpoints/neutral, active-low buttons, real releases, product rejection,
failure/reconnect, stable two-device identities, source-isolated disconnect,
CardKB2 pulses through a 185 ms consumption gap, rejection of impossible held
bindings, and atomic configuration parsing. `tests/i2c_platform.c` compiles the
actual Tactility adapter against narrow host stubs and verifies operation-scoped
device references on success/failure, wrong type, unavailable controller,
backoff/reconnect and close. Native strict-warning tests and
AddressSanitizer/UndefinedBehaviorSanitizer pass. LeakSanitizer is disabled for
the sanitizer run because this workspace's ptrace environment rejects it;
the portable adapter itself performs no dynamic allocation.
