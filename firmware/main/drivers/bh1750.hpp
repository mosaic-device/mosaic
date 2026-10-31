#pragma once
#include "esp_err.h"
namespace mosaic::drivers::bh1750 {
// Opens the sensor's own I2C bus and starts continuous high-resolution measurements.
// Fails with ESP_ERR_NOT_FOUND when nothing answers. Call again to retry after a failure.
// Errors are returned, not logged, so a caller retrying a missing sensor stays quiet.
esp_err_t start();
// The latest measurement, in lux. A measurement takes up to 180 ms, so the first one
// is ready that long after start() and later calls closer together repeat it.
esp_err_t read(float& lux);
}
