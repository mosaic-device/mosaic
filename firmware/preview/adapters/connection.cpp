// Server connection: the preview has no server, so nothing connects. The Server row and the
// console's `trigger` commands set the status the connection would publish.
#include "features/connection/adapters/service.hpp"

namespace mosaic::features::connection::adapters::service {
esp_err_t start() {
    return ESP_OK;
}
} // namespace mosaic::features::connection::adapters::service
