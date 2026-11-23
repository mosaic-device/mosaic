# M3 board verification

Physical-board verification for [#113](https://github.com/mosaic-device/mosaic/issues/113). The board checks passed. The setup below records the prepared configuration and the steps to repeat the run.

## Test setup

| Item                     | Value                                                                                                         |
| ------------------------ | ------------------------------------------------------------------------------------------------------------- |
| Prepared source revision | `11f1946a91b0fa23f8c04de7dabde6020dc14a50` on `main`                                                          |
| Firmware image           | Normal app, `build/m3-verify`, ESP-IDF 5.5.4                                                                  |
| Server and admin         | Development server and built admin assets from the prepared revision. Local preflight used .NET SDK 10.0.404. |
| Target board             | Waveshare ESP32-P4-WIFI6-Touch-LCD-5                                                                          |
| Physical run details     | Flashed revision, running server version, board revision, peripherals and Wi-Fi were unrecorded.              |

## Prepare the server and board

1. Install the tools in [Setup](../setup.md), then run `pnpm install --frozen-lockfile` and `pnpm build:admin-assets` from the repository root.
2. Follow [Reach the server from a device](../../platform/apps/server/README.md#reach-the-server-from-a-device). In the terminal that will run `pnpm server:dev:funnel`, set `Database__Path` to a new absolute SQLite file path for this run. Keep that path for the server restart. If macOS AirPlay Receiver occupies port 5000, free the port before starting the launcher.
3. Check the public HTTPS `/healthz` from another device and expect `200 OK`. Open `http://localhost:5000` on the host and use the server's setup code to create the test admin. Keep the host online when interrupting the board's Wi-Fi later. A separate hotspot for the board makes that possible.
4. Activate ESP-IDF and build from the repository root. Connect the test board's USB-to-UART port. On a board cleared for testing, erase flash before flashing the image. This clears saved Wi-Fi, pairing and device settings.

   ```sh
   git rev-parse HEAD
   dotnet --version
   idf.py --version
   python3 scripts/firmware.py -B build/m3-verify -D SDKCONFIG=build/m3-verify/sdkconfig build
   python3 scripts/firmware.py -B build/m3-verify erase-flash
   python3 scripts/firmware.py -B build/m3-verify flash monitor
   ```

5. Follow [Phone setup](../../firmware/README.md#phone-setup) with the launcher’s `wss://…/ws/device` URL. Confirm setup succeeds, the board reboots and the pairing screen shows an eight-character code. Open **Devices → Pair a device** in the admin app.

## Board checks

| Check                                  | Action and expected observations                                                                                                                                                                                                                                                                                                                                                                                | Result |
| -------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ |
| Invalid code                           | Change one character of the displayed code to another valid character. The admin reports an invalid code and creates no row. The board continues to show its own live code.                                                                                                                                                                                                                                     | Pass   |
| Expired code                           | Keep the board online and wait for the ten-minute code to roll over. Submit the previous code. The admin reports expiry and creates no row. The board displays the replacement code.                                                                                                                                                                                                                            | Pass   |
| Online claim                           | Submit the current code while the board is connected. The admin shows one connected device. The board confirms pairing and opens Home without a reboot. Record the device row for comparison.                                                                                                                                                                                                                   | Pass   |
| Duplicate claim                        | Open the pair form again and submit the same code while the device is still paired. The admin returns to the same device with one row. The board stays paired. Do this before restarting the server, while it still remembers the code.                                                                                                                                                                         | Pass   |
| Remove and reclaim                     | Select **Remove** and confirm **Remove device** while the board is connected. The row disappears and the board shows a fresh code. Submit the original code and confirm the admin reports it was already used. Claim the fresh code and confirm the board opens Home and the admin shows a new connected device. Use this new row for the reconnection checks.                                                  | Pass   |
| Reboot                                 | Power-cycle the board. Verify it opens Home and the same device is connected again in the admin app without a new claim.                                                                                                                                                                                                                                                                                        | Pass   |
| Wi-Fi loss                             | Turn off the board's access point while the host stays online. Wait for the admin row to show offline. Restore Wi-Fi and confirm the same row reconnects and the board remains paired. Record how long each status change takes.                                                                                                                                                                                | Pass   |
| Server restart                         | Stop the Funnel launcher, then restart it with the same database and URL. Confirm public health returns and the board reconnects to the same device row without a new claim.                                                                                                                                                                                                                                    | Pass   |
| Setup recovery                         | On the board, confirm **Settings → Change Wi-Fi or server** and resubmit the same Wi-Fi and exact server URL. Check that it returns to Home and reconnects to the same row after reboot.                                                                                                                                                                                                                        | Pass   |
| Test notification                      | From the connected device's menu choose **Send test**. The admin shows **Delivered** and the board opens the incoming screen. Select **View**, check the detail is visible, then **Back** to dismiss. Confirm Home returns.                                                                                                                                                                                     | Pass   |
| Offline claim after fresh provisioning | Remove the test device row and repeat the erase, flash and phone setup steps. Once the new code appears, record it privately, then power off the board. Keep the server running, allow it to detect the disconnect, then submit the code before it expires. The admin creates one offline device. Power the board on and confirm the same row becomes connected and the board opens Home without another claim. | Pass   |

## Results

All board checks passed as reported. The run surfaced no reported failures.

The local preflight also passed the firmware host tests, server tests, admin tests and simulated-device smoke test.
