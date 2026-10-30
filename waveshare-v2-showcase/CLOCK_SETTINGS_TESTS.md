# Clock settings and local time zones — Waveshare ESP32-S3-Touch-LCD-2.8 V2

The original screensaver hardcoded `EST5EDT,M3.2.0,M11.1.0` in `clock_savers.h`. This was why its clocks showed Eastern time while the RTC and diagnostic SYSTEM readouts were in UTC. That behavior was technically correct, but the zone needed to be configurable.

This update adds **Clock** as page ID 18, reachable in all three navigation layouts under **Wireless and settings**. Existing pages and their IDs remain intact, especially Power=10 and RTC=12. The working QMI8658 Wake-on-Motion code is unchanged.

## Timekeeping model

- **ESP32 system time**: a UTC Unix epoch (the current real-world instant).
- **PCF85063 RTC**: stores a UTC calendar date/time on I²C address `0x51`. The RTC hardware has no concept of time zones or DST.
- **Local display**: `localtime_r()` converts the system epoch using the saved POSIX time zone, including daylight-saving rules. The analog and digital clock screensavers follow this setting.
- **Manual setting**: the six fields are interpreted as **local time in the selected zone**, converted to UTC epoch with `mktime()`, validated with a round trip, and applied to SYSTEM and optionally RTC.

Never add or subtract the time-zone offset when copying SYSTEM UTC to the RTC. That would store the wrong instant.

## Clock panel controls

- **Display time zone**: select UTC, US Eastern/Central/Mountain/Pacific/Alaska/Hawaii, Arizona (no DST), London, Central Europe, India or Sydney. Default **US Eastern**, matching the original showcase.
- **Automatic NTP on Wi-Fi**: enabled by default. When disabled, the active SNTP client is stopped and the choice saved; a manually set time won't be overwritten by future network synchronization.
- **Request NTP now**: explicitly request network time when home Wi-Fi STA is connected. This re-enables automatic NTP and preserves the selected time zone.
- **Sync SYSTEM UTC -> RTC**: copy the system clock to the PCF85063, verifying RTC readback at I²C 0x51.
- **Load current time into editor**: load the local system clock or a valid RTC fallback into six editable fields.
- **Manual date/time**: adjust Year, Month, Day, Hour (24h), Minute and Second with +/−. The editor checks valid month lengths, leap days and nonexistent DST spring-forward times. Autumn's repeated hour is resolved by the C library default when `tm_isdst=-1`; if you need a particular occurrence, synchronize from a precise network UTC time.
- **Set SYSTEM + RTC from editor**: convert the chosen local time into UTC, call `settimeofday`, stop NTP, then update and verify PCF85063. If the RTC write fails, SYSTEM still changes, and the page reports the partial failure.
- **Set SYSTEM only**: change ESP32 clock while leaving RTC hardware unchanged. Useful when checking conversion without touching the RTC.

The top of the Clock panel keeps LOCAL/SYSTEM UTC/RTC UTC readings and operation messages visible while the controls scroll below.

## Persistence and implementation

- NVS namespace: `clock-config`.
- Keys: `tz` (selected index), `ntp` (automatic network time flag).
- `src/time_prefs.h`: zone definitions, persistence, `setenv("TZ")` and `tzset()`, NTP policy.
- `src/clock_panel.h`: LVGL UI, manual edit validation, system/RTC synchronization.
- `src/clock_savers.h`: uses `configTzTime(TimePrefs::posix(), ...)` on Wi-Fi rather than hardcoded Eastern and honors manual/auto NTP preference.
- `src/rtc_lab.h`: its on-demand Request NTP now uses `configTzTime` with the saved zone, instead of `configTime(0,0,...)` that forcibly changed the display TZ to UTC.
- `src/main.cpp`: loads timezone/policy before the clock screensaver, IMU and RTC initialize.

NTP synchronization is asynchronous. The RTC is **not** updated by NTP automatically: use the explicit synchronization button after NTP succeeds.

## Physical regression tests

1. Open **Menu -> Clock**. Confirm local Eastern clock matches your wall clock and SYSTEM UTC is four or five hours ahead depending on DST. RTC should show the same UTC instant if synchronized.
2. Change the zone to **UTC**, then back to **US Eastern**. The *local display* should change while SYSTEM UTC and RTC UTC remain unchanged.
3. Select a different U.S. zone (e.g. Pacific). Check the analog or digital clock screensaver uses the newly selected zone.
4. Turn **Automatic NTP** off. Change date/time in the editor and press **Set SYSTEM + RTC**. Both UTC readouts should reflect the correct converted instant; the LOCAL time should show exactly your entered values. A manual edit automatically turns off NTP as well.
5. Test a nonexistent DST time: US Eastern, 2027-03-14 02:30. It should refuse the setting, leaving the clocks unchanged.
6. Press **Request NTP now** with home Wi-Fi connected. Verify SYSTEM time updates to network time. Press **Sync SYSTEM UTC -> RTC** if you want the hardware RTC corrected.
7. Reboot: check that the zone, automatic NTP setting and clock display survive. Offline time comes from RTC if RTC battery backup is provided and the RTC remains valid.
8. Check **Power -> Deep sleep: MOVE to wake** and back to Clock/RTC. Motion wake must behave exactly as before.

## Build / recovery

Run from the GitHub repository root:

```bash
git pull --ff-only
~/.platformio/penv/bin/platformio run -e waveshare28v2 -t upload
```

The pre-change baseline is saved in branch `waveshare-v2-before-clock-settings`.

**Current status:** source reviewed against Arduino-ESP32 3.3's `configTzTime` behavior and LVGL 9.2 spinbox APIs; not yet compiled or exercised on physical Waveshare hardware.
