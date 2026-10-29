#pragma once

#include "esp_err.h"

namespace mosaic::features::debug_triggers {

// Development builds only (MOSAIC_DEBUG_TRIGGERS). Opens a UART console with `trigger <event>`,
// which publishes through the device event bus exactly as the M3 connection will.
esp_err_t start();

} // namespace mosaic::features::debug_triggers
