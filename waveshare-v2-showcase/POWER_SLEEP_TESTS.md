# Waveshare V2 Power / Sleep Test Lab

This is an **experimental** diagnostics tab. No sleep mode is enabled automatically.

## Verified hardware connections

Waveshare ESP32-S3-Touch-LCD-2.8 published schematic:
- QMI8658C I2C: SDA GPIO11, SCL GPIO10, address 0x6B.
- QMI8658C INT1: GPIO13. INT2: GPIO12 (RTC-capable wake pin).
- BAT_ADC: GPIO8, 200k/100k divider (3:1; approximate voltage only).
- Battery hold: GPIO7 BAT_Control / SYS_EN (keep HIGH).
- LCD backlight: GPIO5.

**Correction:** the QMI8658 interrupts *are* routed to GPIO12 and GPIO13. The previous claim that they were unconnected was incorrect.

QMI8658C datasheet rev 0.6, section 9: low-power Wake-on-Motion (WoM).
- Select INT2, initial LOW, rise on motion; configurable mg threshold.
- Configure CTRL7=0, CTRL2=0x0D (21Hz low power, +/-2g), CAL1_L=threshold mg, CAL1_H=0x48, CTRL9=0x08, poll STATUS1 bit0 for CmdDone, CTRL7=1.
- Deep-sleep wake through ESP32-S3 EXT0 GPIO12 HIGH; parallel 60s timer fallback.
- Reinitialize IMU on reboot. Detect motion vs timer fallback through esp_sleep_get_wakeup_cause().

## Test sequence

1. Flash and open **Power** tab. Verify QMI8658 detected and that INT1/INT2 show live levels.
2. Test **Backlight OFF (10 sec)**; touch or wait ten seconds to restore. The ESP32 remains awake.
3. Test **Light sleep (5 sec)**; Wi-Fi is temporarily stopped and then restored.
4. Test **Deep sleep (15 sec timer)**. The CPU should reboot and show *timer* as the wake reason. If it does not recover, use the physical PWR button and report behavior.
5. With the remote resting flat, choose WoM sensitivity (80mg default) and test **Deep sleep: MOVE to wake**. Motion should wake; otherwise the timer is configured to wake after 60 seconds. Observe the displayed wake reason.
6. **SYS_EN LOW: power OFF** is deliberately separately confirmed. It cannot wake from motion and, on battery power, requires the physical PWR button to restart.

**Important:** GPIO7 is held HIGH across deep sleep using the ESP32-S3 GPIO hold APIs. This behavior needs hardware validation on the actual Waveshare V2. Do not enclose the PWR button until confirmed.

The live battery voltage is an **approximation**, not a current measurement. For actual whole-board mA/uA and realistic battery-runtime forecasts, measure battery supply current externally.

## Manufacturer resources

- https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8
- https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8/Resources-And-Documents
- https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8/FAQ
- https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8/ESP32-S3-Touch-LCD-2.8.pdf
- https://files.waveshare.com/wiki/common/QMI8658C.pdf
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html

Waveshare says the board supports multiple power modes and documents its battery latch, but the accessible board guide does **not** provide V2-specific current measurements for each sleep state.
