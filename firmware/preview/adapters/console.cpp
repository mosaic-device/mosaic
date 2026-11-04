// The debug console on stdin and the window's command line. It runs the firmware's own
// debug commands, plus controls for what the device senses.
#include "../console.hpp"
#include "../device.hpp"
#include "drivers/display.hpp"
#include "esp_system.h"
#include "features/debug_triggers/commands.hpp"
#include "features/debug_triggers/console.hpp"
#include "mosaic_platform.hpp"
#include "ui/navigation/host.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mosaic::preview {
namespace {
    namespace device = preview::device;
    using features::debug_triggers::Command;

    int lux(int argc, char** argv) {
        if (argc == 2) {
            if (std::strcmp(argv[1], "off") == 0) {
                device::set_lux(std::nullopt);
            } else {
                char* end = nullptr;
                const float value = std::strtof(argv[1], &end);
                if (*argv[1] == '\0' || *end != '\0' || value < 0 || value > 100000) {
                    std::puts("usage: lux [0-100000|off]");
                    return 1;
                }
                device::set_lux(value);
            }
        } else if (argc != 1) {
            std::puts("usage: lux [0-100000|off]");
            return 1;
        }
        if (const auto value = device::lux())
            std::printf("lux %.1f\n", *value);
        else
            std::puts("lux sensor unplugged");
        return 0;
    }

    int wifi(int argc, char** argv) {
        constexpr const char* kNames[] = { "joins", "rejects", "silent" };
        if (argc == 2 && std::strcmp(argv[1], "drop") == 0) {
            device::drop_network();
            std::puts("network dropped");
            return 0;
        }
        if (argc == 2) {
            bool known = false;
            for (size_t i = 0; i < std::size(kNames); ++i)
                if (std::strcmp(argv[1], kNames[i]) == 0) {
                    device::set_network(static_cast<device::Network>(i));
                    known = true;
                }
            if (!known)
                argc = 0;
        }
        if (argc != 1 && argc != 2) {
            std::puts("usage: wifi [joins|rejects|silent|drop]");
            return 1;
        }
        std::printf("saved network %s\n", kNames[static_cast<size_t>(device::network())]);
        return 0;
    }

    std::string encode(std::string_view text) {
        std::string out;
        for (const unsigned char c : text) {
            if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                out += static_cast<char>(c);
            } else {
                char escaped[4];
                std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
                out += escaped;
            }
        }
        return out;
    }

    int setup(int argc, char** argv) {
        if (argc == 1) {
            std::puts(device::setup_status().c_str());
            return 0;
        }
        if (argc != 4) {
            std::puts("usage: setup [<ssid> <password|-> <wss-url>]");
            return 1;
        }
        const std::string password = std::strcmp(argv[2], "-") == 0 ? "" : argv[2];
        const std::string body = "ssid=" + encode(argv[1]) + "&pass=" + encode(password) + "&ws_url=" + encode(argv[3]);
        const std::string reply = device::submit_setup(body);
        std::puts(reply.c_str());
        return reply.starts_with("202") ? 0 : 1;
    }

    int storage(int argc, char** argv) {
        if (argc == 2 && (std::strcmp(argv[1], "ok") == 0 || std::strcmp(argv[1], "full") == 0))
            platform::set_nvs_write_failure(std::strcmp(argv[1], "full") == 0);
        else if (argc != 1) {
            std::puts("usage: storage [ok|full]");
            return 1;
        }
        std::printf("storage %s\n", platform::nvs_write_failure() ? "full: writes fail" : "ok");
        return 0;
    }

    int screen(int argc, char** argv) {
        using ui::navigation::Screen;
        if (argc != 2 || (std::strcmp(argv[1], "settings") != 0 && std::strcmp(argv[1], "back") != 0)) {
            std::puts("usage: screen settings|back");
            return 1;
        }
        if (!drivers::display::lock())
            return 1;
        const bool done
            = std::strcmp(argv[1], "back") == 0 ? ui::navigation::back() : ui::navigation::open(Screen::settings);
        drivers::display::unlock();
        if (!done)
            std::puts("not available from this screen");
        return done ? 0 : 1;
    }

    int restart(int argc, char**) {
        if (argc != 1)
            return 1;
        esp_restart();
    }

    int erase(int argc, char**) {
        if (argc != 1)
            return 1;
        platform::erase_nvs();
        std::puts("NVS erased");
        esp_restart();
    }

    int quit(int argc, char**) {
        if (argc != 1)
            return 1;
        std::fflush(stdout);
        std::_Exit(0);
    }

    constexpr Command kPreviewCommands[] = {
        { "lux", "Show or set the light sensor reading, or unplug it", "[0-100000|off]", lux },
        { "wifi", "Show or set how the saved network answers, or drop it", "[joins|rejects|silent|drop]", wifi },
        { "setup", "Show the setup page status, or submit it like a phone", "[<ssid> <password|-> <wss-url>]", setup },
        { "storage", "Show or set whether NVS writes fail", "[ok|full]", storage },
        { "screen", "Open Settings or go back, like the controls would", "settings|back", screen },
        { "restart", "Restart the device, keeping NVS", "", restart },
        { "erase", "Erase NVS and restart, like a factory reset", "", erase },
        { "quit", "Close the preview", "", quit },
    };

    std::mutex g_commands_lock;
    std::vector<Command> g_commands;

    const std::vector<Command>& commands() {
        const std::lock_guard lock(g_commands_lock);
        if (g_commands.empty()) {
            for (const Command& command : features::debug_triggers::commands())
                g_commands.push_back(command);
            for (const Command& command : kPreviewCommands)
                g_commands.push_back(command);
        }
        return g_commands;
    }
} // namespace

int run_command(std::string line) {
    static std::mutex serialized;
    const std::lock_guard lock(serialized);
    return features::debug_triggers::execute(line.data(), commands());
}
} // namespace mosaic::preview

namespace mosaic::features::debug_triggers {
esp_err_t start() {
    // stdin is the device's UART console.
    std::thread([] {
        std::string line;
        while (std::getline(std::cin, line))
            preview::run_command(line);
    }).detach();
    return ESP_OK;
}
} // namespace mosaic::features::debug_triggers
