// Server connection: the preview has no server, so nothing connects. The Server row and the
// console's `trigger` commands set the status the connection would publish, and pairing gets a
// sample code, as the server would send, so the pairing screen can be seen.
#include "events/bus.hpp"
#include "features/connection/adapters/service.hpp"

#include "esp_timer.h"

namespace mosaic::features::connection::adapters::service {
namespace {
    // Ten minutes, as the server gives.
    constexpr uint32_t kCodeLifetimeS = 600;

    void on_status(void*, esp_event_base_t, int32_t, void* data) {
        const auto status = *static_cast<const events::Status*>(data);
        if (status == events::Status::pairing) {
            const auto now_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1'000;
            events::set_pairing_code(events::make_pairing_code("K7QF2M9X", kCodeLifetimeS, now_ms));
        } else if (status == events::Status::connected) {
            events::set_pairing_code({ });
        }
    }
} // namespace

esp_err_t start() {
    return events::subscribe(MOSAIC_EVENT_STATUS_CHANGED, on_status, nullptr, nullptr);
}
} // namespace mosaic::features::connection::adapters::service
