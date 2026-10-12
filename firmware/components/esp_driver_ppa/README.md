# Project-local ESP-IDF PPA driver override

This is the `esp_driver_ppa` component from ESP-IDF **v5.5.4** (Apache-2.0), copied from `components/esp_driver_ppa` with its build file, public header, and production sources. ESP-IDF prefers `firmware/components/esp_driver_ppa` over the identically named component in the shared `IDF_PATH`. Do not edit the shared SDK or `managed_components` for this workaround.

## Local change

Only `src/ppa_srm.c` differs from upstream v5.5.4: in `ppa_srm_transaction_on_picked()`, replace the conditional macro-block-order bypass calculation with `ppa_ll_srm_bypass_mb_order(platform->hal.dev, true)`. This is adapted from Espressif's `0001-bugfix-ppa-Temporary-fix-for-the-PPA-hang-issue.patch` bundled with `espressif/esp_lvgl_adapter` 0.6.4. That patch targets IDF 6.0; the conditional in IDF 5.5.4 uses a different color-depth API, so its patch cannot be applied verbatim. The older patch described on the [Espressif documentation site](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html#ppa-freeze-fix-esp32-p4) sets a register earlier in this function, but IDF 5.5.4 writes the bypass setting again immediately before starting SRM. We use the newer patch's final-write location rather than stacking both patches.

This forces the bypass for **all PPA SRM operations in Mosaic**, not just display refreshes. It is a temporary workaround, **confirmed by the owner on this P4 board** to let the setup screen refresh and show the AP credentials where the unpatched driver stalled. This does not establish reliability for every display geometry or PPA client, and it is not a general-purpose upstream driver improvement. Re-evaluate when changing IDF versions, board revisions, display geometry, or PPA users. Keep the SoftAP fail-closed display check.

To compare against the installed IDF 5.5.4 component (adjust `IDF_PATH` for your environment):

```sh
diff -ru --exclude=README.md --exclude=test_apps "$IDF_PATH/components/esp_driver_ppa" firmware/components/esp_driver_ppa
```

Run `idf.py reconfigure` after adding the override. Check `build/project_description.json` under `build_component_paths` / `build_component_info` to confirm `esp_driver_ppa` resolves to this directory, then rebuild and flash on the board. The normal build's override path and physical setup-screen refresh were confirmed; changes still need regression testing after SDK/adapter upgrades. The upstream `test_apps` directory was not copied; no driver APIs or build flags were changed.
