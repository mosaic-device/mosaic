#pragma once

#include "esp_err.h"

namespace mosaic::features::debug_triggers {

// Development builds only (MOSAIC_DEBUG_TRIGGERS). Opens a UART console with the commands().
esp_err_t start();

} // namespace mosaic::features::debug_triggers
