# Setup

Install only what the pieces you work on need. Versions come from the files that pin them; the tools below read those files, so you don't need to match numbers by hand.

| Tool                                                    | Needed for                                                           | Version pinned by                            |
| ------------------------------------------------------- | -------------------------------------------------------------------- | -------------------------------------------- |
| [Node and pnpm](#node-and-pnpm)                         | Admin, companion, generated files, firmware formatting               | `.nvmrc`, `packageManager` in `package.json` |
| [.NET SDK](#net-sdk)                                    | Server                                                               | `global.json`                                |
| [Rust and Tauri](#rust-and-tauri)                       | Companion                                                            | `rust-toolchain.toml`                        |
| [Python 3](#python-3)                                   | Firmware preview, firmware tests, firmware builds, server smoke test | Any Python 3; CI uses 3.12                   |
| [C++ compiler, CMake, SDL2](#c-compiler-cmake-and-sdl2) | Firmware preview and firmware tests                                  | Any C++23 compiler                           |
| [clang-format](#clang-format)                           | Firmware formatting                                                  | 23.1.2                                       |
| [ESP-IDF](#esp-idf)                                     | Building and flashing firmware                                       | 5.5.4                                        |
| [Docker](#docker)                                       | Building the server's Linux container (optional)                     | Any recent version                           |
| [Tailscale](#tailscale)                                 | Letting a device reach the server on your laptop                     | Any recent version                           |

For example, admin work needs Node, pnpm and .NET. The device preview needs Python, a compiler, CMake and SDL2, but not ESP-IDF.

## Node and pnpm

Install Node with a version manager that reads `.nvmrc`, such as [fnm](https://github.com/Schniz/fnm) or [nvm](https://github.com/nvm-sh/nvm), then let Corepack provide the pinned pnpm:

```sh
fnm install   # or: nvm install
fnm use       # or: nvm use
corepack enable pnpm
pnpm install --frozen-lockfile
```

Run `pnpm install --frozen-lockfile` again whenever `pnpm-lock.yaml` changes.

## .NET SDK

Install the .NET SDK version in `global.json` from [Microsoft](https://dotnet.microsoft.com/download), or with `brew install --cask dotnet-sdk` on macOS. Later patch releases of the same feature band also work. Then restore packages:

```sh
dotnet --version
dotnet restore platform/Mosaic.slnx
```

## Rust and Tauri

Install [rustup](https://rustup.rs/); it selects the toolchain in `rust-toolchain.toml` automatically. Then install the [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/) for your OS: Xcode Command Line Tools on macOS, the WebKitGTK packages on Linux, or the Visual Studio C++ build tools and WebView2 on Windows.

## Python 3

The firmware scripts use `python3`. macOS and Linux usually have it; on Windows, install it from [python.org](https://www.python.org/downloads/) and make sure `python3` runs in your terminal. The firmware tests and preview need only the standard library.

## C++ compiler, CMake and SDL2

The firmware tests compile firmware code with your computer's C++23 compiler (`c++`, or `$CXX` if set). The preview also needs CMake and the SDL2 development files.

| OS      | Install                                                                                                                                                 |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| macOS   | `xcode-select --install`, then `brew install cmake sdl2`                                                                                                |
| Linux   | `sudo apt-get install g++-14 cmake libsdl2-dev` (or your distribution's equivalents), and `export CXX=g++-14` if your default `g++` is older            |
| Windows | Visual Studio 2022 with C++ desktop development, then `vcpkg install sdl2` and set `CMAKE_TOOLCHAIN_FILE` to vcpkg's `scripts/buildsystems/vcpkg.cmake` |

## clang-format

`pnpm fw:format` needs clang-format 23.1.2 on `PATH`; other versions format differently from CI.

```sh
pipx install clang-format==23.1.2
clang-format --version
```

## ESP-IDF

Building and flashing firmware needs ESP-IDF 5.5.4 with the ESP32-P4 toolchain. Install the [prerequisites for your OS](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/get-started/index.html#manual-installation), then:

```sh
git clone -b v5.5.4 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32p4
```

On Windows, use `install.bat esp32p4` instead, or the ESP-IDF Installation Manager.

Activate ESP-IDF in each terminal you build firmware from, then install the component manager version CI uses:

```sh
. ~/esp/esp-idf/export.sh
python -m pip install idf-component-manager==2.5.2
```

On Windows, run `export.ps1` or `export.bat` to activate it. Firmware builds also need Node on `PATH` to generate design tokens; pnpm is not needed.

## Docker

The server's [Linux container](../platform/apps/server/README.md#build-and-run-the-linux-native-aot-container) builds inside Docker, so it needs only [Docker Desktop](https://www.docker.com/products/docker-desktop/) or Docker Engine. Local development doesn't need it.

## Tailscale

A device reaches the server on your laptop through [Tailscale Funnel](https://tailscale.com/kb/1223/funnel), which gives the laptop a public HTTPS address. Only whoever runs the server for a device needs it. Set it up once.

On macOS, use the Homebrew version. Tailscale's docs disagree on whether the Mac App Store and standalone apps support Funnel, but agree that this one does. Don't install it alongside either app.

```sh
brew install tailscale
sudo brew services start tailscale     # runs the Tailscale service and starts it at login
sudo tailscale set --operator=$USER    # lets you run tailscale funnel without sudo
tailscale up                           # prints a sign-in link
```

On Windows, install [Tailscale](https://tailscale.com/download) and sign in; the installer puts `tailscale` on `PATH`. On Linux, follow Tailscale's install steps, then run `sudo tailscale set --operator=$USER` and `sudo tailscale up`.

The first time you run `pnpm server:dev:funnel`, Tailscale asks you to approve Funnel in the browser, which also turns on HTTPS certificates for your tailnet. If it says the machine has no DNS name, turn on MagicDNS under **DNS** in the [admin console](https://login.tailscale.com/admin). To allow Funnel by hand instead, add this to the tailnet policy file under **Access controls** (merge it into `nodeAttrs` if that already exists), and turn on HTTPS certificates under **DNS**:

```json
"nodeAttrs": [
  { "target": ["autogroup:member"], "attr": ["funnel"] }
]
```

Your laptop's address is `https://<machine>.<tailnet>.ts.net` and stays the same while its Tailscale machine name does. Then follow [Reach the server from a device](../platform/apps/server/README.md#reach-the-server-from-a-device).
