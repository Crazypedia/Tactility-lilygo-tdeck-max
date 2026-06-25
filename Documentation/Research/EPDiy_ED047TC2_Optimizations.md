# Research & Optimization Report: ED047TC2 E-ink & EPDiy Controller

This report summarizes optimization techniques, common issues, and troubleshooting findings for the ED047TC2 E-ink display and the EPDiy controller library as used in the Tactility project.

## 1. Performance Optimization Options

| Category | Option | Description | Trade-offs |
| :--- | :--- | :--- | :--- |
| **Refresh Speed** | `epd_set_lcd_pixel_clock_MHz` | Increases the frequency of the LCD peripheral (on ESP32-S3/V7 boards). | Setting too high can cause rendering glitches or data corruption. |
| **Refresh Speed** | `MODE_DU` (Direct Update) | Fastest update mode; drives pixels directly to black or white. Ideal for text input or UI animations. | Causes significant ghosting over time; requires periodic `MODE_GC16` full refreshes. |
| **Refresh Speed** | `MODE_A2` / `MODE_GL16_FAST` | Specialized fast waveforms (if available/custom-loaded). | Often require custom waveform files tailored to specific batches of displays. |
| **Rendering** | `EPD_LUT_64K` | Uses a larger lookup table in internal memory for faster transition calculations. | Uses 64KB of precious internal SRAM. |
| **Rendering** | `EPD_FEED_QUEUE_32` | Increases the line buffer size to prevent the driver from stalling during output. | Higher memory footprint. |
| **LVGL Tuning** | **Render Batching** | Batching multiple partial LVGL flushes into a single EPDiy update. | Increases perceived latency slightly but prevents system-wide task starvation. |
| **Contrast** | **VCOM Calibration** | Setting the correct VCOM voltage (usually printed on the display ribbon). | Incorrect VCOM leads to severe ghosting and permanent "burn-in" artifacts. |

## 2. Common Issues & Troubleshooting

*   **Ghosting & Image Retention:**
    *   *Symptom:* Faint remnants of previous screens remain visible.
    *   *Fix:* Increase frequency of full refreshes (`MODE_GC16`). Use "wave" or "flashing" clears for complete erasure.
*   **System Crashes / Watchdog Timeouts:**
    *   *Symptom:* The ESP32 resets during a screen update, or other tasks (like WiFi/Bluetooth) drop out.
    *   *Cause:* EPDiy update tasks (specifically `epd_prep`) run at extremely high priority with busy-wait loops to maintain timing.
    *   *Fix:* Ensure `epd_hl_update_area` is not called too frequently. The current implementation in `EpdiyDisplay.cpp` uses `isLast` batching, which is a critical optimization.
*   **Temperature Sensitivity:**
    *   *Symptom:* Display looks washed out in the cold or "bleeds" in the heat.
    *   *Fix:* Ensure the `temperature` parameter in update functions is accurate. EPDiy supports an external temperature sensor if available on the board.
*   **PSRAM Contention:**
    *   *Symptom:* Stuttering UI or slow rendering.
    *   *Cause:* EPDiy and LVGL both compete for PSRAM bandwidth.
    *   *Optimization:* Place the LVGL draw buffer in internal SRAM if possible (though 960x540 at 8bpp is too large for most ESP32 internal memory).

## 3. Missed Pitfalls in Current Implementation

*   **Fixed Contrast Threshold:** The current `flushInternal` implementation uses a hard threshold of `127` to convert 8-bit LVGL pixels to 1-bit EPDiy values. While this improves contrast for the `Mono` theme, it completely breaks anti-aliasing and grayscale rendering for images.
    *   *Recommendation:* Add a configuration option to toggle between "High Contrast (1-bit)" and "Grayscale (4-bit)" rendering.
*   **Static HL State:** `EpdiyDisplay.cpp` uses a static `s_hlState`. This prevents the use of multiple E-ink displays and might cause state issues if the display configuration changes at runtime.
*   **Power Management:** Frequent `epd_poweron()` and `epd_poweroff()` calls add significant latency (approx. 100ms+ per update).
    *   *Optimization:* Implement a "dirty" timer that keeps the display powered for a few seconds after an update to allow for rapid successive updates (e.g., during typing).

## 4. Reference Projects for Review
*   **[vroland/epdiy](https://github.com/vroland/epdiy):** The core library. Most optimizations are found in the `scripts/` folder (waveform generation).
*   **[Glider](https://gitlab.com/zephray/glider):** A high-performance E-ink monitor project that uses EPDiy; features advanced waveform management.
*   **[LilyGo-EPD47](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47):** Official driver for the T5-4.7" board. Good reference for hardware-specific power-saving quirks.
