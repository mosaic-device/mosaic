// Host preview: the real firmware, from app_main() down to the drivers, running against
// simulated hardware. This file is the device's case: the window shows the panel, and
// keys and the mouse stand in for the encoder and touch.
#include "console.hpp"
#include "device.hpp"
#include "events/bus.hpp"
#include "features/provisioning/adapters/settings_store.hpp"
#include "console_log.hpp"
#include "device_view.hpp"
#include "mosaic_platform.hpp"
#include "nvs_flash.h"

// The preview has its own main(); SDL does not need to wrap it.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

extern "C" void app_main();

namespace {
namespace device = mosaic::preview::device;
namespace platform = mosaic::preview::platform;
using namespace std::chrono_literals;
constexpr int W = device::kWidth;
constexpr int H = device::kHeight;

// Starting points, reached by preparing what the device would find at boot rather than
// by forcing a screen. Everything after that is the firmware's own behavior.
struct State {
    const char* name;
    bool saved_network;
    device::Network network;
    std::vector<std::string> then; // Console commands once the device is up, retried until they work.
    // How long the firmware takes to get there afterwards, for screenshots.
    std::chrono::milliseconds settle = 2500ms;
};
const std::array<State, 7> kStates { {
    { "setup", false, device::Network::joins, { } },
    { "connecting", true, device::Network::silent, { } },
    // Provisioning gives a saved network its full test timeout before going offline.
    { "offline", true, device::Network::rejects, { }, 21500ms },
    { "unpaired", true, device::Network::joins, { } },
    { "home", true, device::Network::joins, { "trigger connected" } },
    { "trouble", true, device::Network::joins, { "trigger connected", "trigger error" } },
    { "settings", true, device::Network::joins, { "trigger connected", "screen settings" } },
} };

std::vector<std::string> g_args;

const State* find_state(const std::string& name) {
    for (const auto& state : kStates)
        if (name == state.name || (name == "qr" && std::strcmp(state.name, "setup") == 0))
            return &state;
    return nullptr;
}

// esp_restart(): start this program again with the same options and NVS, so the boot
// that follows sees what the firmware saved, as the device would.
void exec(std::vector<std::string> args) {
    mosaic::preview::console_log::stop();
    std::vector<char*> argv;
    for (auto& arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
#ifdef _WIN32
    _execv(argv[0], argv.data());
#else
    execv(argv[0], argv.data());
#endif
    std::perror("Cannot restart the preview");
    std::_Exit(1);
}
std::vector<std::string> without(std::vector<std::string> args, const std::string& option, bool has_value) {
    for (size_t i = 1; i < args.size();) {
        if (args[i] == option)
            args.erase(args.begin() + i, args.begin() + std::min(args.size(), i + (has_value ? 2 : 1)));
        else
            ++i;
    }
    return args;
}
void restart_process() {
    // The boot after a restart keeps the same NVS, including a state's own file. --restarted
    // skips what only applies to the first start: preparing the state and its commands.
    auto args = without(without(g_args, "--command", true), "--restarted", false);
    args.emplace_back("--restarted");
    exec(args);
}
void restart_into(const State& state) {
    auto args = without(without(without(g_args, "--state", true), "--command", true), "--restarted", false);
    args.emplace_back("--state");
    args.emplace_back(state.name);
    exec(args);
}

void prepare(const State& state) {
    platform::erase_nvs();
    ESP_ERROR_CHECK(nvs_flash_init());
    device::set_network(state.network);
    if (!state.saved_network)
        return;
    mosaic::features::provisioning::form::Settings saved { };
    std::snprintf(saved.ssid, sizeof(saved.ssid), "Preview Wi-Fi");
    std::snprintf(saved.pass, sizeof(saved.pass), "preview-password");
    std::snprintf(saved.ws_url, sizeof(saved.ws_url), "wss://mosaic.example.org/device");
    ESP_ERROR_CHECK(mosaic::features::provisioning::adapters::settings::persist(saved));
}

// Runs each command once it succeeds, for steps that need the device to get somewhere first.
std::atomic_bool g_settled { false };
std::atomic_bool g_stuck { false };
void follow(std::vector<std::string> commands, std::chrono::milliseconds settle) {
    std::thread([commands, settle] {
        for (const auto& command : commands) {
            const auto deadline = std::chrono::steady_clock::now() + 15s;
            while (mosaic::preview::run_command(command) != 0) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    std::fprintf(stderr, "Gave up on \"%s\": the device never got there\n", command.c_str());
                    g_stuck = true;
                    break;
                }
                std::this_thread::sleep_for(250ms);
            }
        }
        std::this_thread::sleep_for(settle);
        g_settled = true;
    }).detach();
}

bool save_frame(const std::vector<uint16_t>& frame, const std::filesystem::path& path) {
    auto* image = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint16_t*>(frame.data()), W, H, 16, W * static_cast<int>(sizeof(uint16_t)), SDL_PIXELFORMAT_RGB565);
    const bool saved = image && SDL_SaveBMP(image, path.string().c_str()) == 0;
    if (saved)
        std::printf("Screenshot: %s\n", path.string().c_str());
    else
        std::fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
    SDL_FreeSurface(image);
    return saved;
}

// Match the panel's 5-inch diagonal in desktop coordinates. LVGL and screenshots keep the
// full panel resolution.
int window_width(int requested) {
    if (requested)
        return requested;
    float logical_dpi = 128.0F;
#ifdef __APPLE__
    const auto display_id = CGMainDisplayID();
    const auto millimeters = CGDisplayScreenSize(display_id);
    const auto bounds = CGDisplayBounds(display_id);
    if (millimeters.width > 0 && bounds.size.width > 0)
        logical_dpi = static_cast<float>(bounds.size.width * 25.4 / millimeters.width);
#else
    float reported_dpi = 0;
    if (SDL_GetDisplayDPI(0, nullptr, &reported_dpi, nullptr) == 0 && reported_dpi > 0)
        logical_dpi = reported_dpi;
#endif
    if (logical_dpi < 80.0F || logical_dpi > 200.0F)
        logical_dpi = 128.0F;
    const float panel_width_inches = 5.0F * W / std::hypot(static_cast<float>(W), static_cast<float>(H));
    return static_cast<int>(std::lround(panel_width_inches * logical_dpi));
}

int usage() {
    std::fprintf(stderr,
        "Usage: preview [--state NAME] [--command TEXT]... [--nvs FILE] [--window-width PIXELS] [--shots DIR] "
        "[--screenshot FILE.bmp] [--window-screenshot FILE.bmp]\nStates:");
    for (const auto& state : kStates)
        std::fprintf(stderr, " %s", state.name);
    std::fputc('\n', stderr);
    return 2;
}
} // namespace

int main(int argc, char** argv) {
    g_args.assign(argv, argv + argc);
    std::filesystem::path nvs = "mosaic-preview.nvs";
    std::filesystem::path shots = "preview-shots";
    std::string screenshot_path;
    std::string window_shot_path;
    const State* state = nullptr;
    std::vector<std::string> commands;
    int requested_width = 0;
    bool restarted = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--state" && has_value) {
            state = find_state(argv[++i]);
            if (!state) {
                std::fprintf(stderr, "Unknown state: %s\n", argv[i]);
                return usage();
            }
        } else if (arg == "--command" && has_value) {
            commands.emplace_back(argv[++i]);
        } else if (arg == "--nvs" && has_value) {
            nvs = argv[++i];
        } else if (arg == "--shots" && has_value) {
            shots = argv[++i];
        } else if (arg == "--screenshot" && has_value) {
            screenshot_path = argv[++i];
        } else if (arg == "--window-screenshot" && has_value) {
            window_shot_path = argv[++i];
        } else if (arg == "--window-width" && has_value) {
            requested_width = std::atoi(argv[++i]);
            if (requested_width < 200 || requested_width > 2560) {
                std::fprintf(stderr, "Window width must be between 200 and 2560.\n");
                return 2;
            }
        } else if (arg == "--restarted") {
            restarted = true;
        } else {
            return usage();
        }
    }
    const bool interactive = screenshot_path.empty();
    // A state starts from its own NVS, so presets never touch the device you set up by hand.
    if (state)
        nvs.replace_filename(nvs.stem().string() + "-" + state->name + nvs.extension().string());
    platform::set_nvs_file(nvs);
    platform::set_restart(restart_process);
    if (state && !restarted)
        prepare(*state);

    SDL_SetMainReady();
    if (SDL_Init(interactive ? SDL_INIT_VIDEO | SDL_INIT_EVENTS : 0) != 0) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    if (interactive)
        device::enable_audio();

    // The device's main task: app_main() starts everything and returns.
    std::thread(app_main).detach();
    std::vector<std::string> then = state && !restarted ? state->then : std::vector<std::string> { };
    if (!restarted)
        then.insert(then.end(), commands.begin(), commands.end());
    follow(then, state ? state->settle : 2500ms);

    std::vector<uint16_t> frame(static_cast<size_t>(W) * H);
    auto copy = [&] { device::copy_frame(std::span<uint16_t, W * H>(frame.data(), frame.size())); };

    if (!interactive) {
        const auto deadline = std::chrono::steady_clock::now() + 60s;
        while (!g_settled.load() && std::chrono::steady_clock::now() < deadline) {
            device::run_display();
            std::this_thread::sleep_for(5ms);
        }
        for (int i = 0; i < 20; ++i) {
            device::run_display();
            std::this_thread::sleep_for(5ms);
        }
        copy();
        const bool saved = g_settled.load() && !g_stuck.load() && save_frame(frame, screenshot_path);
        if (!g_settled.load())
            std::fprintf(stderr, "The device did not reach the requested state in time\n");
        std::fflush(stdout);
        // Firmware tasks never end; leave without unwinding under them.
        std::_Exit(saved ? 0 : 1);
    }

    using mosaic::preview::DeviceView;
    mosaic::preview::console_log::start();
    // The screen keeps its physical size; the rest of the device and the bench scale with it,
    // within the desktop.
    int target_width = window_width(requested_width) * DeviceView::width() / W;
    SDL_Rect desktop { };
    if (SDL_GetDisplayUsableBounds(0, &desktop) == 0 && desktop.w > 0 && desktop.h > 0) {
        const float fit = std::min({ 1.0F, desktop.w * 0.95F / target_width,
            desktop.h * 0.9F / (static_cast<float>(target_width) * DeviceView::height() / DeviceView::width()) });
        target_width = static_cast<int>(target_width * fit);
    }
    const int target_height = target_width * DeviceView::height() / DeviceView::width();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    const std::string title = std::string("Mosaic preview") + (state ? std::string(": ") + state->name : "");
    auto* window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, target_width,
        target_height, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    auto* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (window && !renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    // Mouse events arrive in these coordinates, where the screen is 1280x720 panel pixels.
    if (renderer && SDL_RenderSetLogicalSize(renderer, DeviceView::width(), DeviceView::height()) != 0) {
        std::fprintf(stderr, "Cannot set preview coordinates: %s\n", SDL_GetError());
        return 1;
    }
    auto* texture
        = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H) : nullptr;
    if (!window || !renderer || !texture) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    DeviceView view;
    view.start(renderer);
    std::puts("Left/Right or the wheel on the knob: turn. Enter/Space or a click on the knob: press; hold for Back.\n"
              "Backspace: Back. The mouse on the screen: touch. N: notification, E: server trouble, R: restart.\n"
              "F1: console, F12: screenshot, Esc: quit. 1-7: restart into setup, connecting, offline, unpaired,\n"
              "home, trouble, settings. Commands typed here work too; \"help\" lists them.");

    bool running = true;
    int shot_number = 0;
    while (running) {
        bool screenshot = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT
                || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
                running = false;
            if (view.handle(event))
                continue;
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const auto key = event.key.keysym.sym;
                if (key >= SDLK_1 && key < SDLK_1 + static_cast<int>(kStates.size()))
                    restart_into(kStates[static_cast<size_t>(key - SDLK_1)]);
                switch (key) {
                case SDLK_F1:
                    view.open_console();
                    break;
                case SDLK_LEFT:
                    device::turn(-1);
                    break;
                case SDLK_RIGHT:
                    device::turn(1);
                    break;
                case SDLK_RETURN:
                case SDLK_SPACE:
                    device::button(true);
                    break;
                case SDLK_BACKSPACE:
                    device::back();
                    break;
                case SDLK_n:
                    std::thread([] { mosaic::preview::run_command("trigger notification"); }).detach();
                    break;
                case SDLK_e:
                    std::thread([] {
                        const bool trouble = mosaic::events::status() == mosaic::events::Status::error;
                        mosaic::preview::run_command(trouble ? "trigger connected" : "trigger error");
                    }).detach();
                    break;
                case SDLK_r:
                    std::thread([] { mosaic::preview::run_command("restart"); }).detach();
                    break;
                case SDLK_F12:
                    screenshot = true;
                    break;
                case SDLK_ESCAPE:
                    running = false;
                    break;
                }
            } else if (event.type == SDL_KEYUP
                && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE)) {
                device::button(false);
            }
        }
        device::poll_button();
        device::run_display();
        copy();
        SDL_UpdateTexture(texture, nullptr, frame.data(), W * static_cast<int>(sizeof(uint16_t)));
        view.render(texture);
        if (!window_shot_path.empty() && g_settled.load()) {
            // The whole window as drawn, at its output resolution.
            int width = 0, height = 0;
            SDL_GetRendererOutputSize(renderer, &width, &height);
            auto* image = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
            const bool saved = image
                && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, image->pixels, image->pitch) == 0
                && SDL_SaveBMP(image, window_shot_path.c_str()) == 0;
            SDL_FreeSurface(image);
            std::printf(saved ? "Window screenshot: %s\n" : "Window screenshot failed: %s\n",
                saved ? window_shot_path.c_str() : SDL_GetError());
            mosaic::preview::console_log::stop();
            std::_Exit(saved && !g_stuck.load() ? 0 : 1);
        }
        SDL_RenderPresent(renderer);
        if (screenshot) {
            std::filesystem::create_directories(shots);
            char time_text[32];
            const auto now = std::time(nullptr);
            std::strftime(time_text, sizeof(time_text), "%Y%m%d-%H%M%S", std::localtime(&now));
            save_frame(
                frame, shots / (std::string("mosaic-") + time_text + "-" + std::to_string(++shot_number) + ".bmp"));
        }
        SDL_Delay(16);
    }
    view.close();
    mosaic::preview::console_log::stop();
    std::_Exit(0);
}
