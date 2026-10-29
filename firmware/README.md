# ESP32-P4 firmware

Firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-5 using ESP-IDF **5.5.4** and ESP32-C6-hosted Wi-Fi. Display/touch, phone setup, credential save, reboot, Wi-Fi join and recovery have been verified on the physical board. Audio, the production dashboard, pairing and backend transport are not implemented.

## Build and flash

Activate ESP-IDF with the ESP32-P4 toolchain and put Node.js on `PATH`. From `firmware/`:

```sh
python3 ../scripts/firmware.py build
python3 ../scripts/firmware.py flash monitor
```

To use current defaults without changing a local `sdkconfig`:

```sh
python3 ../scripts/firmware.py -B build/softap -D SDKCONFIG=build/softap/sdkconfig build
```

Existing `sdkconfig` values override `sdkconfig.defaults`. Review configuration before flashing. The board has 32 MiB flash and an 8 MiB factory partition; no filesystem image is needed.

- Protocol bindings are committed; regenerate through `platform/packages/protocols`, never by hand.
- CMake regenerates token outputs offline using Node.js. Firmware builds do not require pnpm or asset converters.
- Finished fonts/icons and licenses are committed; see [asset maintenance](../platform/packages/assets/README.md).
- The [local PPA override](components/esp_driver_ppa/README.md) fixes a rotation stall. Reconfigure after changing components and re-test display startup when changing IDF, geometry or PPA usage.
- `dependencies.lock` is committed. Use the wrapper above (or root `pnpm fw:*` commands), not raw `idf.py`: component manager 2.x writes machine-specific local paths. The wrapper expands the committed `${MOSAIC_FIRMWARE_DIR}` placeholder before IDF runs and restores it afterward, preserving dependency updates. It always runs from `firmware/`; paths passed to IDF are relative to that directory. CI must use the same wrapper.
- Do not run firmware commands concurrently. After a forced termination or machine crash, inspect the lockfile, restore only the local driver path to the placeholder, and remove `.mosaic-idf.lock` before retrying. Do not discard genuine dependency updates. Component manager 3.x supports relative paths but requires ESP-IDF 6+.
- Test lock portability without hardware: `python3 ../scripts/firmware_test.py`. These tests simulate IDF in relocated directories; they do not prove a clean ESP-IDF build or physical provisioning.

## Phone setup

1. Scan the displayed Wi-Fi QR or join `Mosaic-XXXXXX` using the displayed password.
2. If the captive portal does not open, visit the displayed **HTTP** URL, normally `http://192.168.4.1/`.
3. Enter the home SSID, password and `wss://` server URL, then tap **Save and reboot**. Passwords must be empty for an open network or 8–63 characters.
4. The board saves settings, shows success for about six seconds, then reboots and joins the saved network.

Setup does not test Wi-Fi connectivity or contact the server. Incorrect credentials are saved too: the device retries Wi-Fi rather than automatically opening an AP. To correct them, tap **Enter setup mode** twice within ten seconds, then submit replacement settings. Storage failures allow retry; if the browser loses the save response, check the device before resubmitting.

Setup credentials must receive display acknowledgement before the AP opens; failure keeps AP/HTTP/DNS closed. The QR joins Wi-Fi but cannot guarantee portal opening. Networks requiring browser sign-in are unsupported.

## Development gallery

The gallery exercises typography, assets and passive controls without starting provisioning, Wi-Fi or reading saved credentials. Keep its configuration separate from normal firmware:

```sh
python3 ../scripts/firmware.py -B build/design-system-gallery -D SDKCONFIG=build/design-system-gallery/sdkconfig -D MOSAIC_UI_GALLERY=ON build
python3 ../scripts/firmware.py -B build/design-system-gallery flash monitor
```

Swipe to browse; interactive controls update the gallery state. **Recreate gallery controls** exercises destruction/recreation. Fixed state samples are not interactive; keyboard navigation and text editing are not implemented. Gallery layout, tint, touch geometry and performance require their own physical review.

Shared helpers live in `main/ui/styles/` and `main/ui/components/`. Create controls inside runtime renderer callbacks and render authoritative feature state; callbacks report actions, not completion. Screens own objects and scrolling; callback contexts must outlive controls. Styles live for the process lifetime. Firmware tokens use desk-distance text sizing and an 80 px minimum control height. Use generated asset symbols and explicit mask recoloring.

## Tests and tooling

From `firmware/`:

```sh
python3 main/features/provisioning/tests/run.py
python3 main/support/tests/run.py
python3 main/ui/tests/run.py
python3 main/events/tests/run.py
clang-tidy -p build main/app_main.cpp
```

From the repository root:

```sh
node --test firmware/main/features/provisioning/tests/setup_ui.test.mjs
pnpm fw:format
pnpm fw:format:check
```

Run analysis only on owned source, not vendor/generated code. Configure clangd with `--query-driver=<absolute path to riscv32-esp-elf-g++>` from `build/compile_commands.json`. `.clangd` adjusts unsupported GCC flags for editor parsing only; do not change firmware compiler flags to fix editor diagnostics.

### Display diagnostics

Use a separate image to distinguish a blocking PPA rotation from a frame-buffer wait:

```sh
python3 ../scripts/firmware.py -B build/display-diagnostics -D SDKCONFIG=build/display-diagnostics/sdkconfig -D MOSAIC_DISPLAY_DIAGNOSTICS=ON build
python3 ../scripts/firmware.py -B build/display-diagnostics flash monitor
```

On refresh timeout, `mosaic_display` logs entry/return counters. More entries than returns identify the blocked operation. Diagnostics are off in normal builds. Startup waits for refresh, a 100 ms settling window and redraw before lighting the panel; reboot blanks and holds the backlight low.

## Architecture and limits

See [provisioning design constraints](main/features/provisioning/README.md#design-constraints) and the [provisioning decision](../docs/adr/0001-softap-provisioning.md).

Setup uses HTTP on a temporary WPA2 AP. Never log credentials or put them in build configuration. Flash/NVS encryption, factory-reset policy, broader phone compatibility, fault injection and LAN-isolation testing remain separate production-readiness work.

## Component provenance

Bundled components retain their source, manifests and licenses:

| Component | Source | License |
| --- | --- | --- |
| `esp32_p4_wifi6_touch_lcd_5` | Waveshare BSP 1.0.1; dependency pins in manifests | Apache-2.0 |
| `esp_lcd_hx8394` | Waveshare 1.0.3, upstream commit `49e44e4` | MIT |
| `esp_driver_ppa` | ESP-IDF 5.5.4 with local workaround; see component README | Apache-2.0 |
| `nanopb` | 0.4.9 | zlib |
