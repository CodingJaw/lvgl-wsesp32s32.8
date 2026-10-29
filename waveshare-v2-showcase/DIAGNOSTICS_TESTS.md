# Waveshare 2.8 V2 — Diagnostics Dashboard

This branch preserves the now-working **QMI8658 Wake-on-Motion deep-sleep test** on the existing Power tab. The new Diagnostics page is an independent, read-only test bench. It does **not** alter IMU calibration, the CTRL1/CTRL9 WoM setup, sleep sources, GPIO7 power latch, Wi-Fi configuration, or screensaver settings.

## What's in Diag (navigation index 9)

- **Touch (CST3530):** last reported x/y, DOWN/UP, GPIO4 interrupt level, successful/failed I2C samples and presses detected while viewing Diag. Updates about every 300ms.
- **Motion (QMI8658C):** three accelerometer axes in **mg**, tilt relative to the Z axis, change between samples in mg, peak motion, GPIO12/13 interrupt levels, and a rolling 30-sample motion trace. Raw accelerometer range is +/-8g (4096 counts/g).
- **Performance:** main-loop iterations/second, LCD SPI flushes/second, pixels/second, average SPI flush time (us), and maximum SPI flush time since boot. **Flushes/s is not the same as FPS.**
- **Memory:** CPU, uptime, reset type, current free/total internal heap and PSRAM, min free since opening Diag and min free since boot, and largest contiguous allocation blocks for heap/PSRAM.
- **Network:** AP enabled state, associated AP client count, AP IP, home-Wi-Fi connection, signal RSSI, and home IP.
- **Power:** approximate battery voltage via GPIO8, GPIO7 battery latch, GPIO6 PWR input, normal backlight PWM setting, and motion-screen-saver settings. A real mA/uA measurement or battery percentage still requires suitable external hardware.

Controls: **Pause** freezes live display updates, **Reset** resets session counters and clears the motion graph (since-boot metrics remain unchanged), **Serial** prints a diagnostic snapshot at 115200 baud. No tests run automatically.

## What to validate after upload

1. Open **Diag**, tap different areas of the display and watch coordinate changes, touches, and I2C error count.
2. Lift, tilt, and rest the board. Verify accelerometer values, the motion graph, and the peak value respond. The graph represents acceleration change between diagnostic samples, not a calibrated position.
3. Visit the JPEG/PNG scenes, then return to **Diag**. Check free PSRAM and the **since-boot** minimums. They should be consistent with image-cache allocations; declining memory over repeated visits merits investigation.
4. Confirm STA/AP details match the Wi-Fi settings page.
5. Compare SPI flush time when static versus scrolling diagnostics; these are SPI timings, not complete display frame times.
6. Press **Serial** and collect the printed [DIAG] lines if a value looks wrong.
7. Return to **Power** and repeat the previously successful **Deep sleep: MOVE to wake** test. It should continue to blank the LCD and wake via QMI8658 INT2. If it doesn't, revert to the backup branch and report the [POWER] logs.

## Performance cautions

- Live diagnostics itself updates screen contents every ~300ms and can increase refresh work and CPU utilization.
- Accelerometer data is read over the same I2C interface as normal wake-detection polling, but the diagnostic page **never writes sensor configuration** and does not enable the gyroscope (which is disabled in normal mode for power saving).
- Touch interrupts and the button GPIO may be too brief to show as LOW in the sampled status text; touch events are tracked separately.
- Some debugging data is available only while viewing Diag; heap and PSRAM *since-boot* low-watermarks remain available across pages and survive Diag counter resets, not CPU resets.
- The displayed battery voltage is approximate. It does not represent charging current, battery capacity, or state-of-charge.

Firmware:
- `src/diagnostics.h` — dashboard and metrics
- `src/main.cpp` — touch sampling, SPI flush timing, and Diag page integration

Upload:
```bash
git pull --ff-only
~/.platformio/penv/bin/platformio run -e waveshare28v2 -t upload
```
