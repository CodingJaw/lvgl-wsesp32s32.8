# Waveshare ESP32-S3-Touch-LCD-2.8 V2 — On-board Hardware Lab

Adds opt-in test panels to the grouped LVGL launcher **without changing the QMI8658 Wake-on-Motion implementation**, the existing page IDs, or screen/scene firmware.

## Board / framework

This repository builds from the **root** `platformio.ini`, using `src_dir = waveshare-v2-showcase/src`, `espressif32@8.0.0` (Arduino-ESP32 **3.3.x**), and the `waveshare28v2` environment.

Manufacturer: https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8

Manufacturer schematic:
https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8/ESP32-S3-Touch-LCD-2.8.pdf

## Menu / test pages

All existing pages stay on their old IDs, including **Diag=9**, **Power=10**, and **Custom Remote=11** (when enabled). The new panels are:

| Page | Menu | Hardware | Tests / behavior |
| --- | --- | --- | --- |
| 12 | RTC | PCF85063 at 0x51, I2C SDA11/SCL10, IRQ9 | Read current UTC, oscillator-stop status, RTC interrupt level; request NTP only on button, then explicitly write UTC to RTC |
| 13 | microSD | SDMMC 1-bit CLK14, CMD17, D0=16 | Mount, capacity/type, list up to 12 files, create **unique new** diagnostic text file on explicit confirmation, read it back; unmount on page exit |
| 14 | Audio | PCM5101 I²S DAC BCK48, LRCK38, DIN47 | 440Hz/880Hz original short tones via Arduino-ESP32 3.x ESP_I2S, bounded software amplitude, hardware volume knob; release I²S on completion/exit |
| 15 | I2C/UART | Shared bus SDA11/SCL10, UART TX43/RX44, GPIO15/18 | Safe I²C address scan; observe un-driven spare GPIOs; UART TX-to-RX loopback after attaching jumper and checking confirmation |
| 16 | Gyro | QMI8658 gyroscope, shared I2C | **Manual** gyro enable at 125Hz / ±512°/s, view three axes in degrees/sec; disable on exit restoring accelerator-only |
| 17 | BLE | ESP32-S3 BLE 5 radio | Default firmware shows disabled state; separate `waveshare28v2-ble` build permits **manual**, one-time 3-second BLE advertisements scan |

The optional Custom Remote page retains ID 11. Tabs, drop-down and Grid launcher skip ID 11 if Custom Remote is disabled.

The **existing Diag tab** also now shows the **CST3530's reported touch-contact count** (including peak). Only the first touch XY is decoded; this is a count-level multitouch diagnostic, *not yet full five-finger coordinate decoding*.

## Safety / power behavior

- No new hardware peripheral tests are started automatically at boot.
- The **RTC** is only written when you deliberately press Sync and system UTC is valid (year 2024–2099). Request NTP does not itself modify the RTC. No RTC alarms are armed by this first test.
- **SDMMC** is mounted only after clicking Mount, never formatted or deleted, and unmounted when leaving its page to avoid affecting deep-sleep battery consumption. The Write test creates a unique `/WS_Dnnn.TXT`; existing files are not overwritten.
- **Audio** is off by default, generates tones only after clicking Play, is capped to 40% digital full scale, and shuts down on exit. Start with the speaker's hardware volume knob low.
- **UART** requires a temporary 3.3V jumper between header TX GPIO43 and RX GPIO44. No GPIO15/18 output mode is used. Do not connect 5V to GPIOs.
- **Gyroscope** consumes additional power while manually enabled. The tested acceleration and motion wake mode is restored when disabling or leaving the Gyro panel.
- **BLE** was deliberately isolated because a different earlier firmware experienced a Bluetooth controller crash. The normal `waveshare28v2` build neither links the test scanner nor starts the BLE controller. In `waveshare28v2-ble`, scanning remains opt-in. Reboot to retry after a scan.
- The working deep-sleep/motion wake implementation in `power_tests.h` was not modified.

## Suggested physical test order

1. **RTC:** Check that the device at 0x51 responds. If its date or oscillator-stop flag is invalid, connect Wi-Fi, request NTP, wait for the system time to become valid, then click Sync RTC. For battery retention, unplug main power and recheck (only expect retention when RTC backup power is correctly connected).
2. **microSD:** Insert a FAT32 card. Mount and list files. For a write test, check Allow and create a *new* diagnostic file, then use Read. Leave the page to unmount.
3. **Audio:** Connect speaker and turn volume low; test 440Hz, then 880Hz. If quiet, inspect speaker/volume connection and try USB power before modifying firmware.
4. **I2C/UART:** Scan the bus. Onboard RTC and QMI8658 should normally answer 0x51 and 0x6B. The eventual TCA8418 (commonly 0x34) and TCA9548A (often 0x70) can be tested once physically wired. For UART test, connect TX43 to RX44 using a temporary jumper then confirm checkbox.
5. **Gyro:** Click Enable, rotate the board, verify deg/s values rise, then click Disable. Return to Power and retest motion wake.
6. **Multitouch:** Open Diag and try one, then two fingers; compare reported contacts. This tests controller reporting, not multi-point gestures.
7. **BLE (separately):** Only after the normal build passes, compile/upload the `waveshare28v2-ble` environment and tap Scan on the BLE page. A 3-second scan may temporarily freeze the touch UI. If BLE initialization crashes, return to the normal build immediately.

## Commands (run from GitHub repository root)

Standard firmware with no Bluetooth code active:

```bash
git pull --ff-only
~/.platformio/penv/bin/platformio run -e waveshare28v2 -t upload
~/.platformio/penv/bin/platformio device monitor -p /dev/ttyACM0 -b 115200
```

Opt-in Bluetooth scan build **only after testing the normal firmware**:

```bash
~/.platformio/penv/bin/platformio run -e waveshare28v2-ble -t upload
```

Return to the known non-Bluetooth build:

```bash
~/.platformio/penv/bin/platformio run -e waveshare28v2 -t upload
```

The prior working branch `waveshare-v2-before-hardware-lab` is preserved as a recovery point.

## Known limitations

- RTC timezone is **UTC** throughout. Time persistence requires RTC backup power; no automatic network time synchronization at startup is introduced.
- The RTC interrupt level is displayed but RTC **alarm / interrupt-based wake** is not configured or tested here.
- SD listing is limited to first twelve root entries and will not attempt to decode SD-hosted photos yet.
- Audio is generated tone output, not MP3 decoding or speaker volume-knob feedback.
- BLE scan is compiled out of the normal firmware and explicitly gated in the optional environment.
- Full multitouch packet decoding, USB HID host/device, power consumption in mA/uA and charger current are not exercised by these panels.
- This change was **source-reviewed** against the current manufacturer schematic, Arduino-ESP32 3.3 APIs and the repository. It has **not been compiled or run on the physical Waveshare** in this environment. Report build errors or on-device symptoms verbatim for a targeted fix.


## PCF85063 RTC fix — October 30, 2026

A critical bug existed in the first RTC page: RTC reads used `MotionWake::readRegs(0x04,...)`, which always performs I²C reads from the QMI8658 address **0x6B**, not RTC **0x51**. RTC writes were sent to 0x51 but the verification step also incorrectly read 0x6B. Consequently the displayed time was not clock data and writes couldn't be verified.

Corrected in `src/rtc_lab.h`:

- All PCF85063 reads and writes now explicitly address 7-bit I²C **0x51** on the existing `MotionWake::imuBus` (SDA GPIO11, SCL GPIO10), which `Saver::begin()` initializes. No second bus startup or reconfiguration.
- Read two control registers at 0x00/0x01, then all seven time registers at 0x04..0x0A in one I²C transaction, as required by NXP's PCF85063A datasheet.
- Validate BCD, weekday, calendar ranges, and the **OS oscillator-stop bit** in Seconds 0x04 bit7. Inspect the independent **STOP** bit at Control1 0x00 bit5. Either condition marks the RTC time invalid or halted.
- The RTC page displays an independent, seconds-updating RTC UTC clock and ESP32 SYSTEM UTC clock, plus a tick count and `RUNNING` / `NOT TICKING` / `INVALID` determination.
- `Request NTP` starts an asynchronous NTP request when Wi-Fi station is connected. This updates only SYSTEM UTC, **not** the PCF85063.
- `Copy SYSTEM UTC -> RTC` explicitly writes a valid system clock into the RTC and reads it back from address 0x51 to verify within one second. On explicit sync only, it clears STOP and selects 24h mode when necessary. Other control/alarm settings are preserved.
- `Copy RTC UTC -> SYSTEM` restores the ESP32's system clock from a valid RTC.
- `Advance RTC by 1 minute` is guarded by an explicit checkbox; it is a temporary test that intentionally changes the RTC. After confirming the minute changes, use `Copy SYSTEM UTC -> RTC` to restore accurate time.
- On startup, after the IMU initializes the shared I²C bus, firmware **reads** the RTC and populates ESP32 system UTC only if the system clock is unset and RTC date, OS, and STOP state are valid. It never writes the RTC automatically, and the proven QMI8658 WoM register sequence is unchanged.
- The page keeps RTC/system clock and error messages fixed above a scrolling controls section, so you can watch the seconds while pressing buttons.

### Recommended RTC re-test

1. Open **RTC** and observe the **RTC 0x51** time for at least five seconds. It should increment once per second and report `Tick:RUNNING`. If not, note `OS`, `STOP`, I²C failure count and message text.
2. Check **SYSTEM UTC**. If it is unset, connect Home Wi-Fi (STA), tap **Request NTP**, then verify the SYSTEM readout shows a plausible time.
3. Tap **Copy SYSTEM UTC -> RTC**, then check RTC time jumps to match SYSTEM UTC and continues incrementing.
4. Check `Allow RTC +1 minute TEST`, tap **Advance RTC by 1 minute**, and verify RTC jumps exactly one minute ahead of SYSTEM. Afterward tap **Copy SYSTEM UTC -> RTC** to restore accurate time.
5. Reboot with Wi-Fi unavailable. If the RTC backup supply is present and the time remains valid, the ESP32 SYSTEM clock should be restored by RTC on startup.
6. Finally, confirm **Power -> Deep sleep: MOVE to wake** still works. This fix does not touch the existing QMI8658 wake code.

**Notes:** The RTC stores **UTC**, not local Eastern time. The value seen on the device may differ from wall time by the time-zone offset. NTP reception is asynchronous and the first 20-second window does not by itself prove a new SNTP packet arrived; the test labels it as such. The RTC backup cell is required for retention if all other board power is removed.

References:
- https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8
- https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf
