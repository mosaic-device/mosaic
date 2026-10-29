#include "console.hpp"

#include "../../events/bus.hpp"

#include "esp_console.h"
#include "linenoise/linenoise.h"

#include <cstdio>
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
} // namespace

esp_err_t start() {
    esp_console_repl_t* repl = nullptr;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "mosaic>";
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (err != ESP_OK)
        return err;

    const esp_console_cmd_t command = {
        .command = "trigger",
        .help = "Post a notification, or set status: pairing, connected, disconnected or error",
        .hint = "<event>",
        .func = trigger,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&command);
    if (err != ESP_OK)
        return err;
    // Echo keystrokes instead of redrawing the whole line on each one; the redraws flicker in idf.py monitor.
    linenoiseSetDumbMode(1);
    return esp_console_start_repl(repl);
}

} // namespace mosaic::features::debug_triggers
