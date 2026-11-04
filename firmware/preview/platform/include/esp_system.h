#pragma once
#include "esp_err.h"
typedef void (*shutdown_handler_t)();
esp_err_t esp_register_shutdown_handler(shutdown_handler_t handler);
// Runs the shutdown handlers, then restarts the preview process with its saved NVS.
[[noreturn]] void esp_restart();
