#include "console.hpp"

#include "commands.hpp"

#include "esp_console.h"
#include "linenoise/linenoise.h"

namespace mosaic::features::debug_triggers {
namespace {
    esp_err_t add(const Command& command) {
        const esp_console_cmd_t registration = {
            .command = command.name,
            .help = command.help,
            .hint = command.hint,
            .func = command.run,
            .argtable = nullptr,
            .func_w_context = nullptr,
            .context = nullptr,
        };
        return esp_console_cmd_register(&registration);
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

    for (const Command& command : commands()) {
        err = add(command);
        if (err != ESP_OK)
            return err;
    }
    // Echo keystrokes instead of redrawing the whole line on each one; the redraws flicker in idf.py monitor.
    linenoiseSetDumbMode(1);
    return esp_console_start_repl(repl);
}

} // namespace mosaic::features::debug_triggers
