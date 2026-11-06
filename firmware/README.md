# Firmware

Firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-5, with Wi-Fi through its ESP32-C6. It doesn't connect to the server yet; use [debug triggers](#debug-triggers) to put the device in connection states. Board results are in [M2 board verification](../docs/hardware/m2-board-verification.md).

Run every command from the repository root. [Setup](../docs/setup.md) lists the tools each command needs.

## Preview

The preview runs the firmware itself in a window on your computer: `app_main()`, every feature, provisioning, the event bus and the screens are the code the board runs. Only the hardware and radio adapters are replaced, by simulated ones in `preview/adapters/`, and `preview/platform/` stands in for the ESP-IDF and FreeRTOS APIs. It needs Python, a C++23 compiler, CMake and SDL2, not ESP-IDF.

```sh
pnpm fw:preview                    # build and start
pnpm fw:preview --fresh            # start as a factory-reset device
pnpm fw:preview --state settings   # start from a prepared device
pnpm fw:preview --state trouble --screenshot firmware/build/trouble.bmp
pnpm fw:preview --build-only
```

Without `--state`, the preview is one device that keeps its storage in `firmware/build/device-preview/device.nvs`: a first start opens Wi-Fi setup, and what you save survives restarts. `--state` starts from a prepared device with its own storage: `setup`, `connecting`, `offline`, `unpaired`, `home`, `trouble` or `settings`. `--screenshot` waits for the screen to settle, saves it and exits; `offline` takes about 20 seconds, the firmware's real Wi-Fi timeout. `--command` runs a [console command](#preview-console) once the device is up, and can be repeated.

The first build uses the managed LVGL component if it is present, otherwise it downloads the same release into `firmware/build/device-preview`; set `LVGL_DIR` to use another copy of that release. The window opens at about the board's 5-inch size; if your monitor reports its size wrong, set `--window-width 600` or similar to match the board.

### Controls

- **Knob:** turn with the mouse wheel over it or Left and Right; press with a click, Enter or Space. Holding past the long-press time is Back, as is Backspace.
- **Touch:** the mouse on the screen.
- **Panel under the device:** **Start as** restarts into a prepared state; **This device** restarts, factory resets or makes storage fail; **Server** sets the connection status or sends a notification; **Saved Wi-Fi** sets how the network answers; **Phone** opens the setup page or submits a ready-made form; **Room light** sets the light sensor or unplugs it.
- **F12** saves a BMP of the screen in `firmware/build/preview-shots`, or the directory given with `--shots`.

While the device shows setup, the preview serves the real setup page at `http://localhost:8080/` (or the next free port; the terminal and the Phone row show it). A phone on the same network can open it at the address shown. Joining the device's own Wi-Fi is the only step it can't simulate.

### Preview console

The terminal is the device's debug console. It accepts the [debug triggers](#debug-triggers) and these commands:

| Command                                  | Simulates                                                               |
| ---------------------------------------- | ----------------------------------------------------------------------- |
| `setup <ssid> <password\|-> <wss-url>`   | A phone submitting the setup page; `setup` alone shows the page's status |
| `wifi joins\|rejects\|silent`            | How the saved network answers; `wifi drop` drops a joined network       |
| `lux <0-100000>\|off`                    | The light sensor's reading, or unplugging it                            |
| `storage ok\|full`                       | Storage writes failing, as on a full partition                          |
| `screen settings\|back`                  | Opening Settings or going back                                          |
| `restart`, `erase`                       | A restart that keeps storage, or a factory reset                        |
| `quit`                                   | Closing the preview                                                     |

## Build and flash

Activate ESP-IDF in your terminal first (see [setup](../docs/setup.md#esp-idf)). Connect the board by USB, then:

```sh
pnpm fw:build
pnpm fw:flash monitor
```

Press Ctrl+] to leave the monitor. `fw:build` uses `firmware/build` and `firmware/sdkconfig`. Values already in `firmware/sdkconfig` override `sdkconfig.defaults`, so after changing the defaults, build in a fresh directory or delete `firmware/sdkconfig`.

Always build through `scripts/firmware.py` (or the `pnpm fw:*` scripts), never raw `idf.py`. The component manager writes machine-specific paths into `dependencies.lock`; the wrapper swaps in your local path while IDF runs and restores the committed placeholder afterwards. It always runs IDF from `firmware/`, so paths you pass are relative to `firmware/`. Only one wrapper command can run at a time. If one was killed, check `firmware/dependencies.lock` for a local path, put the `${MOSAIC_FIRMWARE_DIR}` placeholder back without discarding real dependency updates, and delete `firmware/.mosaic-idf.lock`.

### Build variants

Development builds turn on `MOSAIC_*` options. Give each one its own build directory and `sdkconfig` so it never changes your normal build:

```sh
python3 scripts/firmware.py -B build/<name> -D SDKCONFIG=build/<name>/sdkconfig -D <OPTION>=ON build
python3 scripts/firmware.py -B build/<name> flash monitor
```

| Option                       | Adds                                                                                                     |
| ---------------------------- | -------------------------------------------------------------------------------------------------------- |
| `MOSAIC_DEBUG_TRIGGERS`      | A serial console with the [debug triggers](#debug-triggers). Can't be combined with the gallery.        |
| `MOSAIC_UI_GALLERY`          | A gallery of typography, assets and controls instead of the normal app; no provisioning or Wi-Fi. Swipe to browse. |
| `MOSAIC_ENCODER_TRACE`       | Logs every knob step (`Step +1`), click (`Click`) and long press (`Back`) under `mosaic_encoder`. Combine with the gallery to see focus move. |
| `MOSAIC_BRIGHTNESS_TRACE`    | Logs each light reading that changes the screen or LED brightness.                                       |
| `MOSAIC_DISPLAY_DIAGNOSTICS` | On a display refresh timeout, logs PPA and frame-buffer entry/return counts under `mosaic_display`; more entries than returns show which one blocked. |

Raise logging with these options rather than the global log level: early boot then logs long enough to trip the bootloader watchdog and reset in a loop.

### Debug triggers

With `MOSAIC_DEBUG_TRIGGERS` (and in the preview), the `mosaic>` console accepts:

- `trigger notification`, `trigger pairing`, `trigger connected`, `trigger disconnected`, `trigger error`: publish that event as the server connection will.
- `volume [10-100]`, `mute [on|off]`: show or change those settings.

The normal firmware contains no trigger code; CI checks this.

## Phone setup

1. Scan the Wi-Fi QR code on the screen, or join `Mosaic-XXXXXX` with the password shown.
2. If the setup page doesn't open, visit the **HTTP** address shown, normally `http://192.168.4.1/`.
3. Enter the home network name, its password and the `wss://` server URL, then tap **Save and reboot**. The password must be empty for an open network or 8–63 characters.
4. The device shows success for about six seconds, reboots and joins the network.

Setup doesn't test the network or contact the server, so wrong details are saved too and the device keeps retrying. To change them, tap **Enter setup mode** twice within ten seconds and submit new ones. Networks that need a browser sign-in aren't supported. Design constraints are in the [provisioning guide](main/features/provisioning/README.md) and the [provisioning decision](../docs/adr/0001-softap-provisioning.md).

## Tests and tooling

```sh
pnpm fw:test                 # every firmware host test, no ESP-IDF needed
pnpm fw:test provisioning    # only runners whose path contains "provisioning"
pnpm fw:format               # format owned firmware source
pnpm fw:format:check
```

`fw:test` finds every tracked `tests/run.py` and `tests/*.test.mjs` under `firmware/`, plus the wrapper's lock tests in `scripts/firmware_test.py`, so a new runner needs no registration. Keep decision logic in pure modules without IDF or LVGL includes, so these tests can cover it.

For editor diagnostics, build once, then point clangd at the toolchain with `--query-driver=<absolute path to riscv32-esp-elf-g++>`; it reads `firmware/build/compile_commands.json`. `.clangd` strips GCC flags clangd doesn't understand; don't change compiler flags to fix editor warnings. To run clang-tidy on owned source: `clang-tidy -p firmware/build firmware/main/app_main.cpp`.

## Maintenance

- Protocol bindings in `components/device_protos/generated` are committed; regenerate them through [`platform/packages/protocols`](../platform/packages/protocols/README.md).
- Design tokens are regenerated by every build using Node. Fonts, icons and sounds are committed; see [assets](../platform/packages/assets/README.md).
- The [local PPA override](components/esp_driver_ppa/README.md) fixes a display rotation stall. Re-check display startup on the board after changing ESP-IDF, display geometry or PPA use.
- The board has 32 MiB flash and an 8 MiB factory partition; no filesystem image is needed.
- Setup runs over HTTP on a temporary WPA2 access point. Never log credentials or put them in build configuration.

## Component provenance

Bundled components keep their source, manifests and licenses:

| Component                    | Source                                                   | License    |
| ---------------------------- | -------------------------------------------------------- | ---------- |
| `esp32_p4_wifi6_touch_lcd_5` | Waveshare BSP 1.0.1; dependency pins in manifests        | Apache-2.0 |
| `esp_lcd_hx8394`             | Waveshare 1.0.3, upstream commit `49e44e4`               | MIT        |
| `esp_driver_ppa`             | ESP-IDF 5.5.4 with local workaround; see component README | Apache-2.0 |
| `nanopb`                     | 0.4.9                                                    | zlib       |
