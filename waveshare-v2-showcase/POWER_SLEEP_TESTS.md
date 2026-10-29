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


## Retest after first hardware results (2026-10-29)

Earlier results: backlight woke by touch only; light sleep returned on its timer, not touch; timed deep sleep rebooted to the first tab; motion deep sleep did not blank the display; hardware power-off worked.

### Updated tests

- Live diagnostics include GPIO6 PWR/SYS_OUT, GPIO4 touch IRQ, GPIO12/13 IMU interrupts, boot wake reason.
- Backlight off: touch, PWR or 10-second timeout restores backlight.
- Light sleep: attempts touch IRQ GPIO4 and PWR GPIO6 active-low wake if pins are idle HIGH; 10-second timer fallback. GPIO wake resumes in place.
- Deep sleep: reboot is normal, but Power tab reopens automatically with wake cause and test result.
- IMU motion probe (no sleep): configure QMI8658 WoM, monitor INT2 GPIO12 / STATUS1 for 15 seconds, then restore normal mode. Move device when armed. Reports trigger, timeout, or why configuration failed.
- Deep motion: only try after successful probe. INT2 GPIO12 HIGH wakes via EXT0; timer fallback is 60 seconds. Reports configuration errors.
- SYS_EN off remains separately confirmed; do not treat physical BOOT/RESET as application motion-wake inputs.

### Recommended new test order

1. Watch live PWR/6 and TP/4 values; press PWR and touch display to confirm signals change from 1 to 0.
2. Run backlight off; wake with PWR. Verify it stays on the same tab.
3. Run light sleep; wake with PWR and repeat with touch. Check displayed wake cause: GPIO vs timer.
4. Run timed deep sleep; verify Power tab returns after the 15-second timer with reported wake cause.
5. Run IMU probe; tap then move remote and report whether it says TRIGGERED, TIMEOUT, or shows an error.
6. Only if probe passes, run 60-second motion deep sleep; confirm wake cause EXT0 vs timer.

Serial monitor command: platformio device monitor -p /dev/ttyACM0 -b 115200. Collect [POWER] lines.

Caution: light-sleep touch wake requires active-low idle-high CST3530 IRQ. GPIO4 is skipped if already LOW when arming. Real battery current still requires external measurement.


## WoM driver rework after unsuccessful motion tests

Two successive attempts to arm WoM did not enter deep sleep on the physical
Waveshare V2. The IMU probe also showed no successful interrupt event.

**Root-cause hypothesis (requires hardware retest):** the previous implementation
was coded for QMI8658C datasheet Rev 0.6 CTRL9 handshake, waiting only for
STATUS1 (0x2F) bit 0. The later QMI8658C Rev 0.9 datasheet and Waveshare's
own QMI8658C driver use STATUSINT (0x2D) bit 7 and a CTRL9=0x00
acknowledgment. Manufacturer driver also enables INT2 output with
CTRL1 bit 4. Either mismatch could prevent motion interrupts.

### Changes

- CTRL9 WoM command now waits for the newer STATUSINT 0x2D.bit7 and sends
  CTRL9=0x00 ACK and waits for flag clear. Legacy 0x2F.bit0 completion can
  still be recognized for older sensor firmware.
- CTRL1 bit4 (INT2 output-enable from Waveshare driver) is enabled while
  configuring WoM, then CTRL1 is restored to 0x40 afterward.
- The awake probe **does not turn the display off**; it now samples GPIO12
  before reading STATUS1. QMI8658C datasheet says reading STATUS1 clears
  the WoM event and deasserts the interrupt; repeatedly polling STATUS1
  could hide interrupts.
- The UI reports CMD protocol, chip revision, CTRL1, and precise errors.
- Deep motion turns off the screen only *after* the WoM configuration
  completes and the ext0/timer wake sources are armed; failure leaves
  screen lit and displays a diagnostic message.
- GPIO12 EXT0 wake remains combined with a 60-second timer fallback.
- Deep-sleep wake remains an ESP32 reset; Power tab is automatically shown
  after a successful wake from an initiated deep-sleep test.
- Prior hardware power-latch handling (GPIO7 held HIGH) is preserved.

### Recommended next test

Flash the latest firmware and open the **Power** tab. First press
**Probe IMU: SCREEN STAYS ON**; it deliberately does not go dark.
After it says ARMED, move the board. Look for:

- SUCCESS: INT2 rose HIGH; WoM status=0x...
- WoM CTRL9 timeout: 2D=0x... 2F=0x...
- WoM: INT2 stays HIGH
- TIMEOUT: INT2 did not rise

Only after the awake probe reports SUCCESS should you press
**Deep sleep: MOVE to wake** and check whether it blanks the screen.
If deep wake uses timer instead of EXT0, note the on-screen boot wake
reason.

Manufacturer references:
- QMI8658C datasheet Rev 0.6 (Waveshare):
  https://files.waveshare.com/wiki/common/QMI8658C.pdf
- QMI8658C datasheet Rev 0.9 (QST):
  https://qstcorp.com/upload/pdf/202202/QMI8658C%20datasheet%20rev%200.9.pdf
- Waveshare's newer driver using STATUSINT bit7 and CTRL9 ACK:
  https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2/blob/main/02_Example/Arduino/03_I2C_QMI8658/qmi8658c.cpp
