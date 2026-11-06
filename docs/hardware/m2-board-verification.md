# M2 board verification

What M2 device behavior works on the physical board with every peripheral connected ([#89](https://github.com/mosaic-device/mosaic/issues/89)). Expected behavior comes from the [#56 spec](https://github.com/mosaic-device/mosaic/wiki/Device-Experience-Spec), which the firmware matches since [#115](https://github.com/mosaic-device/mosaic/issues/115). Steps #115 changed are marked for a re-run on the board; the rest kept their results from the first run.

Result values: **Pass**, **Fail** (link the `bug` issue), or **Not tested** (say why). In sections 1 and 2, a step that doesn't depend on the input shows the same result under both, and — means that input can't do the step.

## Setup under test

| Item              | Value                                                                                                                      |
| ----------------- | -------------------------------------------------------------------------------------------------------------------------- |
| Date              | 2026-11-02                                                                                                                 |
| Tester            | Blake Almanza                                                                                                              |
| Firmware revision | `5a334c1` (firmware unchanged from `867869a` on `main`)                                                                    |
| Image             | `build/m2-verify`: `MOSAIC_DEBUG_TRIGGERS`, `MOSAIC_ENCODER_TRACE`, `MOSAIC_BRIGHTNESS_TRACE` on; otherwise the normal app |
| ESP-IDF           | 5.5.4                                                                                                                      |
| Board             | Waveshare ESP32-P4-WIFI6-Touch-LCD-5 (no revision marked on the board)                                                     |
| Power             | USB-C from a MacBook Pro into the board's USB-to-UART port, through the right-angle USB-C extension                        |
| Wi-Fi network     | Open network, band not recorded                                                                                            |

### Wiring

Pin map from the [hardware wiki](https://github.com/mosaic-device/mosaic/wiki/Hardware-Wiring-and-Power), with what was on the bench.

| Connection            | GPIO    | Header pin        | As built                                  |
| --------------------- | ------- | ----------------- | ----------------------------------------- |
| Encoder CLK / A       | 5       | 11                | As listed                                 |
| Encoder DT / B        | 4       | 14                | As listed                                 |
| Encoder SW            | 2       | 8                 | As listed                                 |
| Encoder VCC           | —       | 2 or 18 (3.3V)    | As listed                                 |
| Ring DIN              | 28      | 16                | As listed; 220Ω series resistor           |
| Strip DIN             | 29      | 20                | **not connected**: strip not yet obtained |
| Ring 5V (strip later) | —       | 1 or 3            | As listed; about 4 cm wire; no capacitor  |
| BH1750 SDA / SCL      | 21 / 22 | 15 / 17           | As listed; ADDR unconnected (0x23)        |
| BH1750 VCC            | —       | 2 or 18 (3.3V)    | As listed                                 |
| Speaker               | —       | speaker connector | 8Ω 2W supplied speaker                    |
| Level shifter         | —       | —                 | None                                      |

## Procedure

Build and flash from `firmware/`, then keep the monitor open for the whole session:

```sh
python3 ../scripts/firmware.py -B build/m2-verify -D SDKCONFIG=build/m2-verify/sdkconfig \
  -D MOSAIC_DEBUG_TRIGGERS=ON -D MOSAIC_ENCODER_TRACE=ON -D MOSAIC_BRIGHTNESS_TRACE=ON build
python3 ../scripts/firmware.py -B build/m2-verify flash monitor
```

Start from erased settings so first-run defaults and setup are covered: `python3 ../scripts/firmware.py -B build/m2-verify erase-flash` before flashing. Debug commands are typed at the `mosaic>` prompt. "Both inputs" means do the step once by touch and once with the encoder only (turn to focus, short press to activate, 600 ms hold for Back).

### 1. Boot and setup screens

| #   | Step                                                               | Expected                                                                                                                                                  | Touch                               | Encoder                             |
| --- | ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------- | ----------------------------------- |
| 1.1 | Power on with erased flash                                         | LEDs dark from power-on until a pattern is requested; `LEDs ready` logged; the wordmark alone, then the setup QR with the Wi-Fi name and password visible | Not tested: changed in #115, re-run | Not tested: changed in #115, re-run |
| 1.2 | Join `Mosaic-XXXXXX` from a phone, submit Wi-Fi and a `wss://` URL | Success shown about 6 s, reboot, "Connecting to saved Wi-Fi..."                                                                                           | Pass                                | Pass                                |
| 1.3 | After joining                                                      | "Wi-Fi connected · Waiting for device pairing."; "Enter setup mode" focusable                                                                             | Pass                                | Pass                                |
| 1.4 | Activate "Enter setup mode" once, wait 10 s                        | Label reads "Press again to confirm", then reverts; nothing else happens                                                                                  | Not tested: changed in #115, re-run | Not tested: changed in #115, re-run |
| 1.5 | Activate it twice within 10 s                                      | Setup QR opens; resubmit the same settings to return                                                                                                      | Pass                                | Pass                                |
| 1.6 | Turn off the access point                                          | "Offline", "Can't reach your Wi-Fi. Retrying.", "Enter setup mode" focused; Settings closes if open                                                       | Not tested: changed in #115, re-run | Not tested: changed in #115, re-run |
| 1.7 | Restore the access point                                           | Rejoins and returns to the unpaired screen                                                                                                                | Pass                                | Pass                                |
| 1.8 | Hold encoder (Back) on any setup screen                            | Nothing happens                                                                                                                                           | —                                   | Pass                                |

### 2. Home and Settings

Run `trigger connected` to reach Home.

| #    | Step                                      | Expected                                                                                                                                   | Touch                               | Encoder                             |
| ---- | ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------- | ----------------------------------- |
| 2.1  | `trigger connected`                       | Home: the time, the date and Settings, with Settings focused, also on the first open after boot                                            | Not tested: changed in #115, re-run | Not tested: changed in #115, re-run |
| 2.2  | Back on Home                              | Nothing happens                                                                                                                            | —                                   | Pass                                |
| 2.3  | Open Settings                             | Settings opens, focused on Automatic brightness, also on the first open after boot                                                         | Not tested: changed in #115, re-run | Not tested: changed in #115, re-run |
| 2.4  | Move through every control                | Encoder skips Brightness while automatic is on ("Follows room light" under it) and Volume while sounds are off ("Sounds are off" under it) | —                                   | Not tested: changed in #115, re-run |
| 2.5  | Flip each switch and adjust each slider   | Value changes and applies at once; with the encoder, press to adjust, turn, press to finish                                                | Not tested: changed in #115, re-run | Not tested: changed in #115, re-run |
| 2.6  | Back (on-screen button and encoder hold)  | Home, with focus on Settings                                                                                                               | Pass                                | Pass                                |
| 2.7  | Reopen Settings                           | Focus is where it was left                                                                                                                 | Pass                                | Pass                                |
| 2.8  | Hold Back while adjusting a slider        | Adjusting ends; Settings stays open; turning moves focus again                                                                             | —                                   | Not tested: changed in #115, re-run |
| 2.9  | `trigger error`, then `trigger connected` | Home stays, saying "Can't reach the server. Retrying." while in error                                                                      | Not tested: changed in #115, re-run | Not tested: changed in #115, re-run |
| 2.10 | `trigger disconnected`                    | Unpaired screen returns                                                                                                                    | Pass                                | Pass                                |

### 3. Debug triggers: LEDs and sound

Default settings (volume 60%, sounds on, LEDs on). Start from `trigger disconnected` after a fresh boot. Every change should fade over about a second; nothing blinks or spins.

| #    | Command                                                | Ring                                                               | Strip                            | Sound                                  | Result                              |
| ---- | ------------------------------------------------------ | ------------------------------------------------------------------ | -------------------------------- | -------------------------------------- | ----------------------------------- |
| 3.1  | `trigger pairing`                                      | soft white, breathing slowly                                       | off                              | none                                   | Not tested: changed in #115, re-run |
| 3.2  | `trigger connected`                                    | off                                                                | warm white, steady               | three rising notes (pairing completed) | Not tested: changed in #115, re-run |
| 3.3  | `trigger notification`                                 | blue, and stays blue                                               | swells to blue and back over 3 s | two rising notes                       | Not tested: changed in #115, re-run |
| 3.4  | Turn the encoder or touch the screen                   | blue fades out                                                     | unchanged                        | none                                   | Not tested: changed in #115, re-run |
| 3.5  | `trigger error`                                        | amber, steady                                                      | warm white, steady               | none                                   | Not tested: changed in #115, re-run |
| 3.6  | `trigger connected`                                    | off                                                                | warm white                       | none (a reconnect)                     | Not tested: changed in #115, re-run |
| 3.7  | `trigger disconnected`                                 | off                                                                | off                              | none                                   | Not tested: changed in #115, re-run |
| 3.8  | Repeat a command for the current status                | nothing posted, nothing changes                                    |                                  |                                        | Pass                                |
| 3.9  | `volume 20`, `trigger notification`; then `volume 100` | quieter, then louder from the next cue                             |                                  |                                        | Pass                                |
| 3.10 | `mute on`, `trigger notification`; `mute off`          | no sound while muted; LEDs still change                            |                                  |                                        | Pass                                |
| 3.11 | LED lights off in Settings, run 3.1–3.5                | both outputs stay dark; turning on shows the current pattern       |                                  |                                        | Not tested: changed in #115, re-run |
| 3.12 | Every pattern at the brightest ambient light           | nothing brighter than the 30% ceiling, no random colors or flicker |                                  |                                        | Not tested: changed in #115, re-run |

### 4. Ambient brightness

LEDs here means the ring only; the strip was not connected.

| #   | Step                                             | Expected                                                                                                                                                                      | Result                              |
| --- | ------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------- |
| 4.1 | Boot with sensor connected                       | `Ambient light sensor ready`                                                                                                                                                  | Pass                                |
| 4.2 | Cover the sensor, then shine a light on it       | Screen and LEDs fade (a few seconds) dimmer, then brighter; covered, the LEDs are dimmer than the screen but their colors still recognizable; trace logs readings; no flicker | Not tested: changed in #115, re-run |
| 4.3 | Hold the light level steady near a change point  | Levels don't waver                                                                                                                                                            | Pass                                |
| 4.4 | Automatic brightness off, step brightness 40→100 | Screen and LEDs follow the manual level with a fade                                                                                                                           | Pass                                |
| 4.5 | Unplug the sensor while running                  | One warning under `mosaic_brightness`; fades to fixed levels; no further log spam                                                                                             | Pass                                |
| 4.6 | Plug it back in                                  | `Ambient light sensor ready` within about 5 s; automatic levels resume                                                                                                        | Pass                                |
| 4.7 | Boot with the sensor unplugged                   | One warning; fixed levels; device otherwise normal                                                                                                                            | Pass                                |

### 5. Clock

| #   | Step                             | Expected                                                                              | Result |
| --- | -------------------------------- | ------------------------------------------------------------------------------------- | ------ |
| 5.1 | Boot, before Wi-Fi joins         | `Clock waiting for Wi-Fi`; Home (`trigger connected`) shows `--:--`                   | Pass   |
| 5.2 | After Wi-Fi joins                | `Time synced`; Home shows Arizona time, 12-hour, no AM/PM, matching a reference clock | Pass   |
| 5.3 | Reboot with the access point off | Home shows `--:--`, not the old time                                                  | Pass   |

### 6. Settings persistence

| #   | Step                                                    | Expected                                                                         | Result                              |
| --- | ------------------------------------------------------- | -------------------------------------------------------------------------------- | ----------------------------------- |
| 6.1 | First boot after erase                                  | Defaults: automatic brightness on, brightness 70, sounds on, volume 60%, LEDs on | Not tested: changed in #115, re-run |
| 6.2 | Change every setting, wait 2 s, power cycle             | All changes kept                                                                 | Pass                                |
| 6.3 | Change a setting and power cycle within half a second   | Previous value kept, no warning about an unreadable record                       | Pass                                |
| 6.4 | `volume 30` and `mute on` from the console, power cycle | Kept, and shown in Settings                                                      | Pass                                |

### 7. Power cycles under full load

Run at least five cycles. Before each: Wi-Fi joined, `trigger notification` repeated so the ring, strip and speaker are all active, automatic brightness off at 100 so the LEDs sit at the ceiling. Unplug USB, wait 5 s, plug in. Use the intended wire lengths and USB extension.

| Cycle | LEDs dark at startup | No flicker or random colors | No reset or brownout in the log | Rejoins Wi-Fi | Board, LEDs and regulator not unusually hot | Result     |
| ----- | -------------------- | --------------------------- | ------------------------------- | ------------- | ------------------------------------------- | ---------- |
| 1     |                      |                             |                                 |               |                                             | Not tested |
| 2     |                      |                             |                                 |               |                                             | Not tested |
| 3     |                      |                             |                                 |               |                                             | Not tested |
| 4     |                      |                             |                                 |               |                                             | Not tested |
| 5     |                      |                             |                                 |               |                                             | Not tested |

Also leave it running under that load for 15 minutes and note any reset (`rst:` lines or a reboot banner in the monitor) or heat.

**Not tested.** The strip isn't connected yet, so the board can't be run under the full LED load. Run this section once the strip is wired.

## Results

Tested with the ring, encoder, BH1750 and speaker connected; the LED strip has not been obtained yet.

**#115 re-run pending:** the steps marked "changed in #115" above. Until they are run, the summary below is from the first run.

**Verified:** setup screens and recovery; Home and Settings navigation, Back and focus memory by touch and encoder; every debug trigger's ring pattern and sound, volume, mute and LEDs off; ambient brightness, the sensor-missing fallback and recovery; clock sync and `--:--` without a sync; settings defaults and persistence across power cycles.

**Failed (first run):**

- On the first open after boot, Home and Settings started with nothing focused, so the encoder had to be turned before a press did anything. Fixed in [#115](https://github.com/mosaic-device/mosaic/issues/115); steps 2.1 and 2.3 need a re-run.

**Not tested:**

- Everything involving the LED strip: its patterns, brightness and ceiling, and its data line driven directly from GPIO.
- Power cycles and the soak under full load (section 7), because that needs the strip.

### Not verified

- **Header current capacity and total current draw.** No measurement equipment; the 5V header's rating is unknown and the LED load (about 360 mA at the 30% ceiling) is an estimate. Passing power cycles shows this assembly works, not that it has margin.
- **Notification and notification-detail screens.** Not implemented until M3; only the notification LED and sound feedback is checked here.
- **Pairing, server connection, update required.** M3; the status changes are simulated with debug triggers.

## Differences from #56

None since [#115](https://github.com/mosaic-device/mosaic/issues/115), which settled one behavior for the lights, sounds, brightness and unavailable controls, and brought the firmware and the spec in line. The first run found these differences:

- **LED meanings.** The spec had both lights in the user's color and blue for a notification; the firmware turned the ring off when idle, used spinning, breathing and blinking patterns, red for error, and white for 3 s on a notification.
- **Error sound.** The spec had none; the firmware played two falling notes.
- **Brightness curve.** The spec proposed the strip at about 5–60%; the firmware set 23–100% of the 30% ceiling for both lights.
- **Unavailable controls.** The spec dimmed them with a short reason; the firmware disabled them without one.
