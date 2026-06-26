# LilyGO T-Deck Max — developer notes

Hard-won findings from bringing this board up, including several places where the
vendor documentation, the vendor driver code, and the actual silicon disagree.
Read this before touching the touch, charger, or boot/flash paths.

## Chip identity discrepancies (important)

| Peripheral | Vendor docs say | Actual silicon (this board) | Notes |
|---|---|---|---|
| Touch | "CST328" (pinmap/info.md) | **Hynitron CST66xx** @ 0x1A | The vendor HynTouch driver *probes* cst66xx **before** cst3xx and that is what answers. The CST3530 and CST3xx/CST3240 (`0xD000` 2-byte) protocols return garbage/firmware bytes on this part. CST66xx uses **4-byte register addresses** (see `lib/HynTouch/src/hyn_cst66xx.c` in the vendor repo). |
| Charger | BQ25896 @ 0x6B (older) | **SY6970** @ 0x6A | Newer revisions replaced the BQ25896 with the SY6970 (Silergy). The two are register-compatible for our needs (REG09 bit5 = `BATFET_DIS` ship mode). Confirm with an I2C scan: 0x6A present / 0x6B absent ⇒ SY6970. |
| Fuel gauge | BQ27220 @ 0x55 | BQ27220 @ 0x55 | Matches. Tactility's `Bq27220` driver works as-is. |

I2C address map observed on i2c0 (GPIO13 SDA / GPIO14 SCL, shared bus):
`0x18` ES8311 audio · `0x1A` CST66xx touch · `0x20` XL9555 IO expander ·
`0x28` BHI260AP IMU · `0x34` TCA8418 keyboard · `0x55` BQ27220 fuel gauge ·
`0x5A` DRV2605 haptic · `0x6A` SY6970 charger.

## Touch (CST66xx)

- Driver: `Source/devices/Cst66xxTouch.{h,cpp}` — minimal single-touch LVGL pointer.
- Protocol (from vendor `hyn_cst66xx.c`): reset via XL9555 P07 → delay → normal-mode
  setup (`0xD0000400` ×2, `0xD0000000`, `0xD0000C00`, `0xD0000100`) → read 9-byte
  frame from `0xD0070000` → ACK `0xD00002AB`. First finger: `x = b4 | ((b7&0x0F)<<8)`,
  `y = b5 | ((b7&0xF0)<<4)`, event nibble `b8>>4` (0 = up). Identity: read
  `0xD0030000`, expect `buf[2]==buf[3]==0xCA`.
- **The touch controller needs a full POWER-CYCLE (not an ESP32 RST) to clear a
  latched/stuck state.** An RST or reflash does not power-cycle it, and the P07
  reset pulse does not always clear it. Symptom: reads return a single frozen
  frame, or firmware-looking bytes. If touch goes dead after button reboots,
  unplug power fully — note a connected battery also keeps it powered.
- Orientation flags (swapXy/mirrorX/mirrorY) are currently all false and not yet
  validated against the panel.

### Bezel touch-buttons (heart / speech-bubble / paper-airplane)

The three printed buttons below the screen are **hardware touch-keys on the same
CST66xx**, not separate GPIOs. They arrive in the normal touch frame: `buf[3]`
high nibble = key count, and when a key is present the first slot's `buf[8]` holds
`id = b8 & 0x0F`, `state = b8 >> 4` (non-zero = pressed). The controller reports
keys *mutually exclusively* with finger coordinates, so a key frame never carries
a touch point. `Cst66xxTouch::handleBezelKey` does rising-edge detection and maps
each id to a navigation action.

Confirmed key ids (left → right) and their **default** mapping:

| Button | key id | Action | Call |
|---|---|---|---|
| ❤️ heart | 0 | Back | `tt::app::stop()` (no-op at launcher root) |
| 💬 speech-bubble | 1 | Home | `tt::app::start("Launcher")` |
| ✈️ paper-airplane | 2 | Recents | `tt::app::start("AppList")` |

**To customise:** edit the `switch` in `Cst66xxTouch::handleBezelKey`
(`Source/devices/Cst66xxTouch.cpp`) — change the action per `BEZEL_KEY_*` case.
E.g. launch a specific app instead (`tt::app::start("<AppId>")`), or swap which
icon does what. The `BEZEL_KEY_HEART/SPEECH/AIRPLANE` constants are the ids; the
case body is the action.

## Power / charging (SY6970 + BQ27220)

- Driver: `Source/devices/TdeckmaxPower.{h,cpp}`. Battery metrics via BQ27220;
  `powerOff()` sets SY6970 ship mode (REG09 bit5, `BATFET_DIS`), matching the
  vendor's XPowersLib `PowersSY6970::shutdown()`.
- **Ship-mode power-off only fully powers down on battery with USB UNPLUGGED.**
  With USB connected the system rail stays up (this is the charger's behaviour,
  not a bug). The red power LED is on the always-on rail and will not go out while
  USB is attached.
- The physical PWR button is wired to the charger's **/QON pin, not an ESP32 GPIO**
  — firmware cannot read it. Its behaviour is fixed in silicon:
  short press = power on from ship mode; **hold ~10 s (TQON_RST) on battery =
  hardware force-reset**. There is no firmware "long-press to power off."

## Boot / display / build gotchas

- ESP-IDF lives in-repo at `.espressif/v5.5.2/esp-idf`; source `export.sh` before
  building. Do NOT use the IDE "build" button — it runs bare ninja with a stale
  `IDF_PATH`. Flow: `python device.py lilygo-tdeckmax` → `idf.py build` →
  `idf.py -p /dev/ttyACM0 flash monitor`. After `device.py` regenerates sdkconfig
  the first `idf.py build` may fail on a missing `Generated/devicetree.c` — just
  build again.
- LVGL config comes from Kconfig/sdkconfig (`CONFIG_LV_CONF_SKIP=y`), NOT
  `lv_conf.h`. Set it via `device.properties [lvgl]`. `colorDepth` must be **16**
  (esp_lvgl_port refuses to compile with `LV_COLOR_DEPTH_1`); the GDEQ031T10
  driver renders monochrome itself via `LV_COLOR_FORMAT_I1`. Use `theme=Mono` —
  the default colour theme thresholds to invisible on a 1bpp panel.
- e-paper specifics: the I1 framebuffer needs +8 bytes for the LVGL palette
  header; the panel write exceeds the default SPI transfer size, so the spi0 node
  needs `max-transfer-size = <65536>`; a full refresh holds the LVGL lock long
  enough to trip GuiService's 5 s timeout, so the board uses `RefreshMode::Fast`.
- i2c0 uses `espressif,esp32-i2c-master` (the CST66xx/`i2c_controller` API needs
  the new master driver, not the legacy one).

## Serial console quirks (ESP32-S3 native USB)

- Console = primary UART0 + secondary USB-Serial-JTAG mirror on `/dev/ttyACM0`.
  It only streams while the app is actively logging — an **idle device is silent**
  (don't mistake that for a dead port).
- RTS toggling does NOT reset the chip. `python -m esptool --port /dev/ttyACM0
  --after hard_reset run` resets it, but the USB CDC re-enumerates so an immediate
  re-open can miss the boot logs. The port streams reliably right after `app-flash`.

## Known upstream bug exposed (not board-specific)

Leaving the **Files** app can hang the loader: an app transition calls
`tt::app::files::View::deinit` → `lv_obj_remove_event_cb(dir_entry_list, …)` on a
freed object, so `lv_obj_get_event_count` reads garbage and the `loader_dispatch`
task busy-spins (task watchdog fires, touch/idle starve). This is in core
Tactility, not in any board file. As a safeguard, `CONFIG_ESP_TASK_WDT_PANIC` +
UART core dump are enabled (see `Buildscripts/sdkconfig/default.properties`) so a
hang auto-reboots with a decodable backtrace instead of wedging.

## Software state (2026-06-25)

Working on hardware: display, keyboard navigation, touch (navigation + tap-to-open),
battery status, power-off button. All merged to the local `main`.

- **Touch tap-to-activate**: UI buttons fire on `LV_EVENT_SHORT_CLICKED`, which needs a
  press shorter than LVGL's long-press time (default 400ms). The e-paper refresh of a
  button's pressed state blocks LVGL's input loop ~0.8-1.2s, so every tap looked like a
  long-press and never clicked. Fix: `lv_indev_set_long_press_time(indev, 2500)` in
  `Cst66xxTouch::startLvgl`. (Touch I2C and display SPI are separate buses — not a hw
  conflict.) Proper long-term fix: decouple input polling from the blocking flush.
- **Windowed partial refresh** (see Gdeq031t10Display): diffs frame vs shadow, refreshes
  only the changed region via the UC8253 partial-window sequence; full refresh every 8
  partials / on >3/4-screen changes / requestFullRefresh. Built-in waveforms only (no
  custom LUTs). Big navigation responsiveness win.
- **Cursor blink disabled** on e-paper (chained theme sets LV_PART_CURSOR anim_duration=0)
  to stop text fields self-refreshing.

### Known issues / TODO
- **Keyboard uppercase/numbers**: vendor keymap matches hardware; layers are HOLD-based
  (hold ALT -> uppercase, hold SYM -> symbol/number layer, ALT+SYM -> caps toggle).
  Verify which printed layer maps to which modifier and align keymap_uc/keymap_sy.
- **Wi-Fi screen periodic full refresh**: the Wi-Fi view re-renders its whole content area
  every 1-2s (app-level e-paper UX, affects any e-paper device). Not a board bug.
- **Intermittent crash a short while after Wi-Fi connect**: shows Tactility's QR crash
  screen (backtrace is *encoded in the QR* as `https://oops.tactilityproject.org?...&s=<hex PCs>`).
  To diagnose: scan the QR, split the `s=` hex into PC addresses, `addr2line` against
  `build/Tactility.elf`. `CONFIG_ESP_TASK_WDT_PANIC=y` is active; the UART-coredump config
  did not take (needs a Kconfig choice) — use the QR, or wire coredump-to-flash if needed.
- **SD card + app-store/downloads**: not yet tested (next up).

### Done since
- **Bezel touch-buttons** (heart/speech-bubble/paper-airplane) → Back/Home/Recents.
  See the "Bezel touch-buttons" subsection under [Touch](#touch-cst66xx).
