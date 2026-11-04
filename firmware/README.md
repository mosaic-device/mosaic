# ESP32-P4 firmware

Firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-5 using ESP-IDF **5.5.4** and ESP32-C6-hosted Wi-Fi. Display/touch, phone setup, credential save, reboot, Wi-Fi join and recovery have been verified on the physical board. The production dashboard, pairing and backend transport are not implemented; device states can be injected with [debug triggers](#debug-triggers). The M2 on-board check and its results are in [M2 board verification](../docs/hardware/m2-board-verification.md).

## Device screen preview

The preview runs the firmware itself on your computer: `app_main()`, every feature
service, provisioning, the event bus and the screens are the same code the board
runs. Only the hardware and radio adapters are replaced, by simulated ones in
`preview/adapters/`, and `preview/platform/` stands in for the ESP-IDF and
FreeRTOS APIs. A change to firmware behavior shows up in the preview without
preview changes.

Install CMake, a C++23 compiler and SDL2 development files: `brew install cmake sdl2`
on macOS, `sudo apt-get install cmake libsdl2-dev` on Linux, or on Windows Visual
Studio 2022 with SDL2 from vcpkg (`vcpkg install sdl2`, then set
`CMAKE_TOOLCHAIN_FILE` to vcpkg's toolchain). From the repository root, run:

```sh
pnpm fw:preview
pnpm fw:preview --build-only
pnpm fw:preview --fresh
pnpm fw:preview --state settings
pnpm fw:preview --state trouble --screenshot firmware/build/trouble.bmp
```

Without `--state`, the preview is one device that keeps its NVS in
`firmware/build/device-preview/device.nvs`: a first start opens Wi-Fi setup, and
what you save survives restarts. `--fresh` erases it. `--state` starts from a
prepared device instead, using its own NVS: `setup`, `connecting`, `offline`,
`unpaired`, `home`, `trouble` or `settings`. States set up what the device finds
at boot (saved Wi-Fi, how the network answers) and the triggers it receives;
the firmware gets itself to the screen. `--screenshot` waits for that, saves
the frame and exits; `offline` takes about 20 seconds, the firmware's real
Wi-Fi test timeout.

The first build uses the managed LVGL 9.4.0 component if present, otherwise it
downloads that release into `firmware/build/device-preview`. Set `LVGL_DIR` to use another
local copy of the same release. The preview uses the monitor's physical size
to open at approximately the board's 5-inch diagonal. If the monitor reports
an inaccurate size, use `pnpm fw:preview --window-width 600` to calibrate
against the board. The firmware screen and screenshots stay at 1280×720 in
RGB565; the window uses the monitor's high-DPI output for a sharp image.

The window shows the whole device: the screen, the LED ring around the knob to
its right, the LED strip along the bottom, and the light sensor and speaker on
its top edge.
Turn the knob with the mouse wheel over it or Left and Right, and press it with
a click or Enter or Space; holding past the long-press time is Back, as is
Backspace. The mouse on the screen is touch. The layout constants at the top of
`preview/device_view.cpp` place the ring, strip and sensor.

The panel under the device drives the world around it, one labeled row each:
**Start as** restarts into the states above; **This device** restarts, factory
resets, or makes storage fail; **Server** sets the connection status or sends a
notification; **Saved Wi-Fi** sets how the network answers or drops it;
**Phone** opens the setup page or submits a ready-made form while the device
shows setup; and **Room light** sets the sensor's reading or unplugs it. The current choice in each row
is highlighted. Press F12 to save a BMP of the screen in
`firmware/build/preview-shots`, or set a different directory with `--shots`.

While the firmware shows setup, the preview serves its real setup page at
`http://localhost:8080/` (or the next free port; the terminal and the Phone row
show it). A phone on the same network can open it at this computer's address,
also shown. Requests get the device's answers from the same rules: form
validation, throttling, the status the page polls, then the save and restart.
Only joining the device's own Wi-Fi is missing, since the preview has no radio;
the QR code and `192.168.4.1` on screen are what the board would show.

The terminal is the device's debug console, for anything the panel does not
cover. It has the [debug triggers](#debug-triggers) plus the commands the panel
uses:

| Command | Simulates |
| --- | --- |
| `setup <ssid> <password\|-> <wss-url>` | A phone submitting the setup page; `setup` alone shows the page's status |
| `wifi joins\|rejects\|silent` | How the saved network answers; `wifi drop` drops a joined network |
| `lux <0-100000>\|off` | The light sensor's reading, or unplugging it |
| `storage ok\|full` | NVS writes failing, as on a full partition |
| `screen settings\|back` | Opening Settings or going back, as the controls would |
| `restart`, `erase` | A restart keeping NVS, or a factory reset |
| `quit` | Closing the preview |

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

## Debug triggers

Navigation, LED and speaker feedback follow `MOSAIC_EVENT` events on a dedicated ESP-IDF [`esp_event`](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/api-reference/system/esp_event.html) loop, wrapped by `main/events/bus.hpp`. `MOSAIC_EVENT_STATUS_CHANGED` carries the connection status (disconnected, pairing, connected, error) and is posted only when it changes; `MOSAIC_EVENT_NOTIFICATION_RECEIVED` is posted once per notification. Features subscribe with `events::subscribe()` after `events::start()`; handlers run on the `mosaic_events` task and must hand blocking work (audio, LVGL) to their own task. A post that cannot be queued within 100 ms is logged and returned as an error. The M3 connection will publish here. Until then, a development-only image adds a serial command that publishes through the same bus:

```sh
python3 ../scripts/firmware.py -B build/debug-triggers -D SDKCONFIG=build/debug-triggers/sdkconfig -D MOSAIC_DEBUG_TRIGGERS=ON build
python3 ../scripts/firmware.py -B build/debug-triggers flash monitor
```

At the `mosaic>` prompt on the UART console, run `trigger notification`, `trigger pairing`, `trigger connected`, `trigger disconnected` or `trigger error`. Posted events log `Status: <name>` or `Notification` under `mosaic_events`; setting the current status again posts nothing. `volume [10-100]` and `mute [on|off]` show or change those [device settings](#device-settings), which are saved like any other change. Provisioning and Wi-Fi run normally. The option is off by default and cannot be combined with the gallery; the CI firmware build checks that the normal image contains no trigger code.

## Tests and tooling

From `firmware/`:

```sh
python3 main/features/provisioning/tests/run.py
python3 main/support/tests/run.py
python3 main/ui/tests/run.py
python3 main/events/tests/run.py
python3 main/drivers/tests/run.py
python3 main/features/led_feedback/tests/run.py
python3 main/features/audio_cues/tests/run.py
python3 main/features/brightness/tests/run.py
python3 main/features/clock/tests/run.py
python3 main/features/settings/tests/run.py
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

## Rotary encoder

A KY-040 on GPIO 5 (A/CLK), 4 (B/DT) and 2 (switch) is an LVGL encoder: turning moves focus and pressing activates. Power the module from 3.3V, never 5V, because its pull-ups go to its VCC pin. The PCNT peripheral counts edges in hardware, and `drivers/encoder_logic.cpp` turns the count into one step per detent, realigning at each rest position. `espressif/button` debounces the switch. A press shorter than 600 ms clicks when released, and a latch hands each click to LVGL so a tap between input reads still clicks. Holding the button for 600 ms is Back: it never reaches LVGL, so it cannot also click or toggle a slider's edit mode. If the encoder fails to start, the UI logs it and keeps touch input.

Steps and presses are logged at Debug, which normal builds compile out. For a board check, use a gallery build in its own directory (its focusable controls show focus moving) with `MOSAIC_ENCODER_TRACE`, which raises only the encoder's logs:

```sh
python3 ../scripts/firmware.py -B build/encoder-trace -D SDKCONFIG=build/encoder-trace/sdkconfig -D MOSAIC_UI_GALLERY=ON -D MOSAIC_ENCODER_TRACE=ON build
python3 ../scripts/firmware.py -B build/encoder-trace flash monitor
```

Boot logs `Rotary encoder ready`; each click logs `Step +1`/`Step -1` (a fast spin may log larger steps) and each short press logs `Click` and each long press logs `Back` under `mosaic_encoder`. Do not raise the global default log level instead: early boot then logs long enough to trip the bootloader watchdog and reset in a loop.

## Screens and navigation

`main/ui/navigation/navigator.cpp` is the device's screen map, and `main/app/screens.cpp` connects it to the features:

| Screen | Shown when | Back |
| --- | --- | --- |
| Boot | until provisioning reports its first state: the wordmark only | nothing |
| Setup | provisioning's screens: setup QR, connecting, offline, and "Wi-Fi connected · waiting for pairing" (unpaired) | nothing |
| Home | Wi-Fi online and paired: the time, the date, and a line saying when the server is out of reach | nothing |
| Settings | opened from Home | Home, with focus back on Settings |
| Notification | reserved for M3, above Home or Settings | the screen underneath |

Home appears once the device is paired: connecting proves it until M3 stores the pairing. A paired device that loses the server (status `error`) stays on Home, which says so; `disconnected` or `pairing` means unpaired and returns to provisioning's unpaired screen. With debug triggers, `trigger connected` shows Home and `trigger disconnected` leaves it. Losing Wi-Fi, or setup credentials appearing, brings provisioning's screens to the front and closes Settings, because they offer setup.

Touch and the encoder share focus. Each screen has its own focus group, so turning moves through the visible screen's controls only (skipping unavailable ones) and a screen remembers its focus when you return. Every screen with a control starts with its first control focused, so a press works without turning first. A short press activates the focused control, the same as a tap; on a slider it starts adjusting, turning changes the value, and another press finishes. Holding the encoder button for 600 ms is Back, as is the on-screen Back button; while adjusting a slider, Back finishes adjusting instead. Back with nowhere to go does nothing.

Every screen uses the same page layout from `main/ui/components/`: screen margins, a header row with the wordmark or title, and controls a touch target tall. Any touch, turn or press tells the [LEDs](#led-ring-and-strip) that a waiting notification has been seen.

`main/ui/navigation/host.cpp` gives each screen a container and focus group inside the runtime's single renderer. Provisioning's renderer keeps its setup credentials handshake: credentials bring its screen to the front before they render. M3 adds the notification screen as `Screens::notification` and opens it with `navigation::open()`.

## LED ring and strip

The WS2812 ring (8 LEDs) takes data on GPIO 28 and the strip on GPIO 29, both driven directly from 3.3V GPIO by the RMT peripheral; see the [hardware wiki](https://github.com/mosaic-device/mosaic/wiki/Hardware-Wiring-and-Power) for power and the series resistor. The strip length is set to the planned 12 LEDs in `main/drivers/leds.hpp`; change it there once the strip is cut. The driver starts first in `app_main`, sends a dark frame and blanks the LEDs again on restart. Every value it sends passes through `limit()` in `main/drivers/leds_logic.hpp`, which scales full-scale colors onto the 30% ceiling (76/255 per channel).

`main/features/led_feedback/` shows the device status and notifications. Each light means one thing. The ring, around the knob, lights only when something wants the owner; the strip is ambient light, on whenever the device is paired. Nothing blinks or spins: every change crossfades over 0.8 s.

| State | Ring | Strip |
| --- | --- | --- |
| Not paired (`disconnected`), including setup | off | off |
| Pairing (`pairing`) | soft white, breathing slowly (3 s) | off |
| Paired and idle (`connected`) | off | warm white, steady |
| Server out of reach (`error`) | amber, steady | warm white if paired, otherwise off |
| New notification | blue, steady, until the owner touches, turns or presses something | swells to blue and back over 3 s |

A waiting notification takes the ring over any status color; once seen, the ring shows the status again. Becoming unpaired clears it. Colors are full scale; [ambient brightness](#ambient-brightness) dims both lights.

To check them on the board, flash the [debug triggers](#debug-triggers) image and run each `trigger` command. The boot log shows `LEDs ready` under `mosaic_leds` with the pins, lengths and ceiling.

## Device settings

Settings, opened from Home, adjusts device feedback. `main/features/settings/settings.cpp` defines them:

| Setting | Control | Default | Range |
| --- | --- | --- | --- |
| Automatic brightness | switch | on | on or off |
| Brightness (while automatic is off) | slider | 70 | 40-100 perceived, steps of 10 |
| Sounds | switch | on | on or off |
| Volume | slider | 60% | 10-100%, steps of 10 |
| LED lights | switch | on | on or off |

Each change applies at once. A control that cannot do anything right now is dimmed with the reason under its name, and turning the encoder skips it: Brightness says "Follows room light" while automatic brightness is on, and Volume says "Sounds are off" while sounds are off. Volume never reaches zero, since silence is the Sounds switch; an older saved volume of 0 loads as 10. Turning the LEDs off keeps both outputs dark while their patterns keep running underneath, so turning them on shows the current one.

Settings are saved a second after the last change, so stepping through several values writes flash once. They live in their own NVS namespace, `mosaic_device`, as one versioned record, separate from provisioning's `mosaic_net`. On first boot the defaults apply quietly. If the record is unreadable or not valid, the device logs a warning under `mosaic_settings` and uses the defaults; a failed save is logged and the change still applies until restart. Startup never waits on storage.

## Clock

Home shows the time as 12-hour local time without AM/PM, such as `2:05`, in a 200 px digits-only font (`mosaic_clock_200`), with the date under it, such as `Tuesday, November 3`. Each time Wi-Fi joins, `main/features/clock/` syncs over SNTP from `pool.ntp.org`, and ESP-IDF re-syncs hourly while connected. Startup never waits for it.

Until a sync succeeds this boot, Home shows `--:--` and no date, and it stays that way if sync never works. A time kept across a restart without Wi-Fi, or one earlier than 2026, is never shown as correct. Boot logs `Clock waiting for Wi-Fi` under `mosaic_clock`, and the first sync logs `Time synced`.

**Time zone:** every device uses Arizona time (`MST7`: UTC-7 all year, no daylight saving), where Mosaic is developed. It is `kTimeZone` in `main/features/clock/clock.hpp`; the owner's zone can come from setup or the server later. To check it on the board, flash the [debug triggers](#debug-triggers) image, let Wi-Fi join, and run `trigger connected` to show Home.

## Speaker cues

Cues play through the board's ES8311 codec and the speaker connector, using the BSP's `bsp_audio_codec_speaker_init()`. Opening the codec for a cue turns the amplifier on, and closing it afterwards turns it off, so the amplifier is on only while a cue plays. Each cue starts after 50 ms of silence, while the DAC and amplifier settle, and ends with 80 ms of silence that flushes the I2S buffers before the amplifier switches off. Every cue logs `i2s_channel_disable(…): the channel has not been enabled yet` from `i2s_common`: the codec library reconfigures I2S on each open without checking that the previous close already disabled it. It is harmless; avoiding it would mean keeping the codec, and so the amplifier, on between cues.

`main/features/audio_cues/cues.cpp` decides which events are heard. The device makes a sound only when there is something to hear about:

| Event | Cue |
| --- | --- |
| notification | `notification`: two rising notes |
| status `connected`, when pairing completes or for the first time since starting | `connected`: three rising notes |
| reconnecting after the server or Wi-Fi dropped | none, so a blip at night stays silent |
| `error`, `pairing`, `disconnected` | none; the screen explains them |

Volume (default 60%) applies from the next cue. Turning sounds off skips cues without opening the codec, and stops a playing cue within about 10 ms. Both are [device settings](#device-settings). If cues arrive faster than they play, up to four wait and the rest are dropped.

The cues are 22050 Hz mono 16-bit WAV files in `main/features/audio_cues/generated/`, synthesized from a spec in `platform/packages/assets` and original to Mosaic; change them there, not here. To check them on the board, flash the [debug triggers](#debug-triggers) image and run `trigger notification` and `trigger connected` (after `trigger pairing` to hear it again), with `volume` and `mute` in between.

## Ambient brightness

A BH1750 light sensor sets the screen and LED brightness. It has its own I2C bus: SDA on GPIO 21, SCL on GPIO 22, 100 kHz, address 0x23 (ADDR low or unconnected), separate from the BSP's touch and audio bus on GPIO 7/8. Power the breakout from 3.3V: its pull-ups go to its VCC, and the P4's pins must never see 5V. The P4's weak internal pull-ups are also enabled as a backstop.

`main/features/brightness/policy.cpp` turns readings into levels. It works in perceived brightness (0-100, where 50 looks half as bright as 100) and converts to PWM only for the drivers, using CIE 1976 lightness, because eyes judge light roughly logarithmically and equal PWM steps look uneven. In a dark room the LEDs drop further than the screen: they sit at the edge of the owner's view, and a lit strip in the dark draws more attention than the screen.

| Light | Screen, perceived (PWM) | LEDs, perceived (share of the 30% ceiling) |
| --- | --- | --- |
| 1 lux or darker | 45 (15%) | 35 (8%) |
| 20 lux | 65 (34%) | 60 (28%) |
| 150 lux | 85 (66%) | 85 (66%) |
| 600 lux or brighter | 100 (100%) | 100 (100%) |

With automatic brightness off in [device settings](#device-settings), a manual level replaces the curve for the screen and LEDs alike, and fades in the same way. Between points, levels follow log(lux). Readings every 200 ms are smoothed over about a second. Levels change only once the light moves more than about 26% from where they were last set, so light wavering near a point never flickers them, and each change fades in equal perceived steps over a few seconds. The LED level only dims below the 30% ceiling, and the display driver never lets the backlight drop below 10%, because the setup credentials handshake treats a lit panel as visible.

If the sensor is missing at startup or stops responding, the device logs one warning under `mosaic_brightness`, fades to fixed levels (perceived screen 85, LEDs 85) and retries every 5 s without logging; it logs `Ambient light sensor ready` when the sensor returns.

To check it on the board, build with `MOSAIC_BRIGHTNESS_TRACE`, which logs each reading that changes the levels:

```sh
python3 ../scripts/firmware.py -B build/brightness-trace -D SDKCONFIG=build/brightness-trace/sdkconfig -D MOSAIC_BRIGHTNESS_TRACE=ON build
python3 ../scripts/firmware.py -B build/brightness-trace flash monitor
```

Cover the sensor and shine a light on it; the screen and LEDs should fade without flicker. Unplug the sensor to see the fallback, and plug it back in to see it recover.

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
