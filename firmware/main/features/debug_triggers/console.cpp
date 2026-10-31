#include "console.hpp"

#include "../../events/bus.hpp"
#include "../settings/service.hpp"

#include "esp_console.h"
#include "linenoise/linenoise.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mosaic::features::debug_triggers {
namespace {
    namespace events = mosaic::events;

    int trigger(int argc, char** argv) {
        events::Status status;
        esp_err_t err;
        if (argc == 2 && std::strcmp(argv[1], "notification") == 0) {
            err = events::post_notification();
        } else if (argc == 2 && events::parse(argv[1], status)) {
            err = events::set_status(status);
        } else {
            std::printf("usage: trigger notification|pairing|connected|disconnected|error\n");
            return 1;
        }
        if (err != ESP_OK) {
            std::printf("not posted: %s\n", esp_err_to_name(err));
            return 1;
        }
        return 0;
    }

    int volume(int argc, char** argv) {
        if (argc == 2) {
            char* end = nullptr;
            const long percent = std::strtol(argv[1], &end, 10);
            if (*argv[1] == '\0' || *end != '\0' || percent < 0 || percent > 100) {
                std::printf("usage: volume [0-100]\n");
                return 1;
            }
            auto settings = features::settings::current();
            settings.volume = static_cast<uint8_t>(percent);
            features::settings::set(settings);
        } else if (argc != 1) {
            std::printf("usage: volume [0-100]\n");
            return 1;
        }
        std::printf("volume %u%%\n", features::settings::current().volume);
        return 0;
    }

    int mute(int argc, char** argv) {
        auto settings = features::settings::current();
        if (argc == 2 && (std::strcmp(argv[1], "on") == 0 || std::strcmp(argv[1], "off") == 0)) {
            settings.muted = std::strcmp(argv[1], "on") == 0;
            features::settings::set(settings);
        } else if (argc != 1) {
            std::printf("usage: mute [on|off]\n");
            return 1;
        }
        std::printf("mute %s\n", features::settings::current().muted ? "on" : "off");
        return 0;
    }

    esp_err_t add(const char* name, const char* help, const char* hint, esp_console_cmd_func_t func) {
        const esp_console_cmd_t command = {
            .command = name,
            .help = help,
            .hint = hint,
            .func = func,
            .argtable = nullptr,
            .func_w_context = nullptr,
            .context = nullptr,
        };
        return esp_console_cmd_register(&command);
    }
} // namespace

esp_err_t start() {
    esp_console_repl_t* repl = nullptr;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "mosaic>";
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (err != ESP_OK)
        return err;

    err = add(
        "trigger", "Post a notification, or set status: pairing, connected, disconnected or error", "<event>", trigger);
    if (err == ESP_OK)
        err = add("volume", "Show or set the saved cue volume (0-100)", "[0-100]", volume);
    if (err == ESP_OK)
        err = add("mute", "Show or set the saved cue muting", "[on|off]", mute);
    if (err != ESP_OK)
        return err;
    // Echo keystrokes instead of redrawing the whole line on each one; the redraws flicker in idf.py monitor.
    linenoiseSetDumbMode(1);
    return esp_console_start_repl(repl);
}

} // namespace mosaic::features::debug_triggers
