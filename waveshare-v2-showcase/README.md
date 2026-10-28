# Waveshare ESP32-S3-Touch-LCD-2.8 V2 LVGL Showcase

This directory is reserved for a standalone PlatformIO showcase application using this fork's LVGL demos.

## Hardware
- Waveshare ESP32-S3-Touch-LCD-2.8 **V2**
- ST7789, 240 x 320 SPI display
- CST3530 I2C touch controller (do not substitute CST328/GT911 drivers)

## Integration plan
1. Import the verified display/touch initialization from the existing Waveshare V2 swipe-test firmware.
2. Pin a compatible LVGL version and enable the matching demo sources.
3. Start with the Widgets demo; add Music, Benchmark and Stress demos individually.
4. Add a launcher and collect flush timing, touch latency and free heap/PSRAM metrics.
5. Validate on hardware before changing clock rates, DMA, Wi-Fi or Bluetooth.

## Status
Scaffold only. No firmware has been compiled or flashed. The existing working PlatformIO project is required to preserve the validated pin mapping and touch implementation.

## Safety
Do not flash a firmware built for the Waveshare 2.8B/ST7701 or CST328 variants to the V2 board.
