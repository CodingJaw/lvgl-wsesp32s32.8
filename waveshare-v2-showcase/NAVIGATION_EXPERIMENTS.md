# Navigation experiments — Waveshare ESP32-S3-Touch-LCD-2.8 V2

The original horizontal strip becomes difficult to use as the firmware gains more test panels. This branch offers **three navigation layouts** without changing any panel IDs or the working QMI8658 deep-sleep Wake-on-Motion implementation.

## Choose a layout

Tap the persistent **Menu** button at the top of any regular showcase page. The full-screen launcher has three style buttons near the top:

| Menu style | On-screen navigation | Best for |
| --- | --- | --- |
| Tabs | Fixed Menu button + horizontally scrolling panel tabs | Keeping the original familiar interface |
| Select | Fixed Menu button + drop-down list of all visible panels | Fast access with a small header |
| Grid | Fixed Menu button + current title + previous/next arrows | One-handed use and quick exploration |

**Grid** is the initial default. The chosen style is saved in NVS Preferences namespace `nav-layout`, key `layout`. Layout changes take effect immediately and persist through restart and deep sleep. All three styles keep a **Menu** button for changing modes.

The grouped launcher has sections:

- Interface tests: Widgets, Controls, Chart, List.
- Graphics and scenes: Scenes, PNG, JPEG.
- Hardware diagnostics: Diag, Power.
- Connections and settings: Wi-Fi, Saver.
- Remote: your optional custom remote, shown only when enabled through the web configuration portal.

The launcher also names **RTC, microSD, Audio, BLE** as future test panels, **not** working functions. They can be added in these groups as we implement their hardware tests.

## Test checklist

1. On first boot, confirm a compact `Menu | Widgets | < >` header (unless NVS already saved another style). Press right-arrow repeatedly to cycle through all enabled pages, then left to reverse.
2. Press **Menu** to open the grouped launcher. Scroll to **Hardware diagnostics** and press **Diag**. Verify it loads and keeps reporting touch and accelerometer data.
3. Press **Menu** again and open **Power**. Confirm the deep-sleep motion test still blanks the LCD and wakes via motion. After wake, the Power page should be visible and the new navigation layout should still be selected.
4. Switch to **Select**. Verify that the dropdown shows exactly the enabled panels, that changing selection navigates, and that Menu remains available.
5. Switch to **Tabs**. Verify the Menu button remains fixed while you swipe across tab names, and that the currently selected tab is highlighted.
6. Switch back to **Grid**. Check opening and closing Menu doesn't accidentally change the active page.
7. Enable or rename the optional Custom Remote panel through the existing web UI. Its entry should update immediately in Tabs, Select, and the launcher; disabling it while displayed should return to Widgets.
8. From PNG/JPEG, enter the full-screen scene viewer, swipe scenes, and exit. The existing viewer gestures must remain unchanged.

## Development notes

- `src/navigation.h` holds the navigation UI and page metadata. Use `Navigation::begin(root, show_page)` during setup and `Navigation::setCurrent(page)` after a page is drawn.
- The original `show_page(int)` function continues to render every existing panel; its numeric IDs are **unchanged**. Power is page 10, Diag page 9, optional Custom Remote page 11.
- `Navigation::refresh()` rebuilds the current menu if the web-configured custom remote changes.
- The launcher is a temporary LVGL overlay with vertically scrolling category rows; it uses asynchronous deletion when closed from within its own event callback.
- No new GPIO, accelerometer register writes, deep-sleep sources, or wireless services were added.
- If you don't like the new navigation, select **Tabs** or restore backup branch `waveshare-v2-before-nav-experiments`.

## Build/upload

From the existing showcase PlatformIO project:

```bash
git pull --ff-only
~/.platformio/penv/bin/platformio run -e waveshare28v2 -t upload
```

This code was integrated and source-reviewed against LVGL 9.2 APIs. No local PlatformIO compiler or attached Waveshare hardware is available here, so the physical display/menu behavior still needs validation on the board.


## Hardware Lab extension (October 2026)

The menu now includes the first set of additional hardware tests. Existing pages retain their IDs: Diagnostics=9, Power=10, optional Custom=11. RTC=12, microSD=13, Audio=14, I2C/UART=15, Gyro=16, and BLE=17. Tabs, selector, and grid navigation all **skip optional page 11** when the custom remote is disabled; new hardware pages remain reachable.

The grouped launcher no longer labels RTC, microSD, Audio and BLE as unimplemented. Details, setup warnings and the test order are in [HARDWARE_LAB_TESTS.md](HARDWARE_LAB_TESTS.md).

The normal firmware leaves Bluetooth disabled and the BLE test separate behind the `waveshare28v2-ble` PlatformIO environment.
