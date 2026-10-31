#pragma once
#include "settings.hpp"
#include "esp_err.h"
namespace mosaic::features::settings {
// Loads the saved settings, or the defaults if storage fails, and applies them to
// brightness, audio cues and LED feedback. Storage problems are logged and never
// block startup. Call once, after NVS and those features start.
esp_err_t start();
// Any task.
Settings current();
// Any task. Each applies at once; changes are saved a second after the last one,
// so turning through several steps writes flash once.
void change(Change change);
void set(const Settings& settings);
}
