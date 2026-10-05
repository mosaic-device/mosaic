# Minimal ESP32-P4 firmware scaffold

Board: Waveshare ESP32-P4-WIFI6-Touch-LCD-5; ESP-IDF **5.5.4**, target `esp32p4`. The C++23 `app_main` only logs a boot message. It does not initialize the display, touch, audio, Wi-Fi, LVGL, or a protocol transport.

## Build

Install and activate ESP-IDF 5.5.4 with the ESP32-P4 toolchain. From this directory:

```sh
idf.py --version
idf.py set-target esp32p4
idf.py build
```

`platform/packages/protocols` in the parent project owns `components/device_protos/generated/events.pb.c` and `events.pb.h`. Those **committed, generated outputs must be present** before configuring/building; do not hand-write substitutes. `main/ui/generated/design_tokens.h` belongs to the parent token generator; this minimal image does not use it. Neither generated directory is created here. The `device_protos` component compiles `events.pb.c` against the included Nanopb 0.4.9 runtime. A user-provided `idf.py build` log now shows a linked ESP32-P4 image; flashing and runtime behavior still need verification.

The local BSP and display driver manifests pull in their required display/touch/codec/LVGL dependencies through IDF Component Manager. No audio-player, WebSocket, Wi-Fi remote/Hosted, LED-strip, or button dependency is added by `main`. The BSP's own adapter may resolve transitive components. Do not copy `managed_components/`, `build/`, or credentials. `sdkconfig.defaults` selects a custom 8 MiB factory partition on the board's 32 MiB flash; the remaining space is intentionally unallocated at this stage. No filesystem image is bundled.

`dependencies.lock` is ignored until a clean ESP-IDF 5.5.4 resolve has been inspected for actual component versions and transitive dependencies. The BSP/driver direct registry versions below are pinned in the component manifests. Review and commit a clean firmware lock as a separate reproducibility decision.

## Formatting and analysis

Format only firmware-owned source, not vendor or generated C:

```sh
clang-format -i main/app_main.cpp
clang-format --dry-run --Werror main/app_main.cpp
# After a successful IDF configure produces build/compile_commands.json:
clang-tidy -p build main/app_main.cpp
```

Run `clang-tidy` only on owned source paths; the `.clang-tidy` header filter is scoped to `main/`. Do not pass vendor or generated C files to the check. For editor diagnostics, `.clangd` points clangd at `build/compile_commands.json`. On macOS, `.clangd` replaces ESP-IDF's GCC-specific `-march=rv32imafc_zicsr_zifencei_xesploop_xespv` with the base RISC-V ISA for editor parsing only; firmware builds still use the original toolchain flags. clangd also needs the cross-compiler's system headers: configure Zed's clangd arguments with `--query-driver=<absolute path to the ESP-IDF riscv32-esp-elf-g++ binary>` (find the path in `build/compile_commands.json`). Do not add an explicit `--target` in `.clangd`: clangd passes it to GCC during driver discovery, which fails. This configuration was checked locally with Apple's `/usr/bin/clangd` and `clangd --check=main/app_main.cpp --query-driver=<path>`; upstream LLVM (`brew install llvm`) remains an option if Apple clangd cannot handle other toolchain flags. Restart the language server after changing its arguments or rerunning IDF configuration. The project `.clangd` also removes the confirmed GCC-only `-fno-shrink-wrap`, `-fno-tree-switch-conversion`, and `-fstrict-volatile-bitfields` flags from editor parsing only. If LLVM clangd reports other unsupported flags, review them individually; do not edit ESP-IDF build flags or assume compiler errors are false. Exceptions and RTTI remain disabled by IDF defaults; no project option enables them. Secrets are not used by this minimal image; `sdkconfig.secrets.defaults.example` is only a local-file convention for later features.

## Component provenance

The component sources, headers, manifests, documentation and licenses are included in this repository. Generated device bindings come from `platform/packages/protocols`.

| Component                    | Version/source                                                                                                                                                                                                                                   | License                                                                              |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------ |
| `esp32_p4_wifi6_touch_lcd_5` | Waveshare BSP manifest 1.0.1, [Waveshare components](https://github.com/WaveshareCloud/Waveshare-ESP32-components); direct registry versions narrowed to codec 1.6.2, GT911 1.2.1, LVGL adapter 0.6.4, HX8394 1.0.3                              | bundled `LICENSE` (Apache-2.0)                                                       |
| `esp_lcd_hx8394`             | Waveshare manifest 1.0.3, [upstream commit `49e44e4`](https://github.com/waveshareteam/Waveshare-ESP32-components/commit/49e44e4a5b81951f3af25f6a12c481b58d20ae18); direct registry versions narrowed to CMake utilities 0.5.3 and I2C bus 1.5.2 | bundled `license.txt` (MIT)                                                          |
| `nanopb`                     | `NANOPB_VERSION` = `nanopb-0.4.9`; [upstream 0.4.9](https://github.com/nanopb/nanopb/tree/0.4.9)                                                                                                                     | upstream 0.4.9 `LICENSE.txt` (zlib) |

The direct registry pins have **not** been independently validated against a separate clean resolve. The generated device bindings and board components now compile/link into `build/device_firmware.bin` according to the user-provided build log. Confirm the ESP-IDF version in the activated shell, review the resolved lockfile, and flash/test on hardware before claiming board bring-up.
