#pragma once
#include "esp_err.h"
esp_err_t esp_register_shutdown_handler(void (*)());
