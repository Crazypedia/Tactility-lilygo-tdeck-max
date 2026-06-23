# LilyGO T-Deck Pro Max

Board support for the LilyGO T-Deck Pro Max (ESP32-S3, 3.1" 320x240 e-paper).

Pin assignments and I2C addresses are taken from the vendor reference
[Xinyuan-LilyGO/T-Deck-MAX](https://github.com/Xinyuan-LilyGO/T-Deck-MAX)
(`lib/TDeckMaxBoard/src/TDeckMaxBoard.h`, `docs/pinmap.md`).

## Wired up

| Peripheral      | Driver            | Bus / pins                          |
|-----------------|-------------------|-------------------------------------|
| E-paper display | GDEQ031T10        | SPI2 CS=34 DC=35 RST=9 BUSY=37      |
| Touch           | CST3530           | I2C0 @ 0x1A, INT=12                  |
| Keyboard        | TCA8418           | I2C0 @ 0x34, backlight=42           |
| I/O expander    | XL9555            | I2C0 @ 0x20                          |

At boot the XL9555 releases the touch (expander pin 7) and keyboard (expander
pin 9) reset lines, and the shared-SPI LoRa (GPIO3) and SD (GPIO48) chip-selects
are deasserted so they can't glitch the e-paper bus.

## Not yet implemented

BHI260AP IMU, DRV2605 haptics, BQ27220 fuel gauge, SY6970 charger, ES8311 audio,
SX1262 LoRa, GPS, and the 4G modem are not wired up yet.

## Needs hardware verification

This board has not been validated on real hardware. The most likely things to
need adjustment:

- **Touch I2C address (0x1A).** The vendor pinmap labels the touch chip "CST328";
  if touch doesn't respond, scan the I2C bus and adjust the address in
  `Source/devices/Display.cpp`.
- **Keyboard column order.** Tactility's TCA8418 driver decodes columns straight
  while the vendor decodes them reversed, so `Source/devices/TdeckmaxKeyboard.cpp`
  reverses columns at lookup (`COLUMNS_REVERSED = true`). If keys come out
  mirrored left-to-right, set it to `false`.
- **Keyboard ALT/SYM layers.** Only the base (lowercase) layer is transcribed
  from the vendor source; the uppercase and number/symbol layers are derived and
  should be confirmed/adjusted against the real layout.
- **E-paper BUSY polarity and refresh timings** in the GDEQ031T10 driver.
