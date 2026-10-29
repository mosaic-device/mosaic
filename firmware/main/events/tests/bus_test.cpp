// Exercises the bus wrapper against a synchronous esp_event fake. The bus is process-global,
// so this runs one ordered scenario.
#include "../bus.hpp"

#include "freertos/semphr.h"

#include <cassert>
#include <vector>

using namespace mosaic::events;

namespace {
struct Recorder {
    std::vector<Status> statuses;
    int notifications = 0;
    bool status_matched_data = true;
};

void on_event(void* arg, esp_event_base_t base, int32_t id, void* data) {
    auto* recorder = static_cast<Recorder*>(arg);
    assert(base == MOSAIC_EVENT);
    if (id == MOSAIC_EVENT_STATUS_CHANGED) {
        const Status posted = *static_cast<Status*>(data);
        recorder->statuses.push_back(posted);
        recorder->status_matched_data &= status() == posted;
    } else {
        assert(id == MOSAIC_EVENT_NOTIFICATION_RECEIVED && data == nullptr);
        ++recorder->notifications;
    }
}
} // namespace

int main() {
    Recorder all, notifications;

    // Nothing works before the loop exists.
    assert(subscribe(ESP_EVENT_ANY_ID, on_event, &all, nullptr) == ESP_ERR_INVALID_STATE);
    assert(set_status(Status::connected) == ESP_ERR_INVALID_STATE);
    assert(post_notification() == ESP_ERR_INVALID_STATE);
    assert(status() == Status::disconnected && fake_posts == 0);

    // Failed starts clean up and can be retried.
    fake_semaphore_create_fails = true;
    assert(start() == ESP_ERR_NO_MEM);
    fake_semaphore_create_fails = false;
    fake_loop_create_result = ESP_FAIL;
    assert(start() == ESP_FAIL);
    fake_loop_create_result = ESP_OK;
    assert(start() == ESP_OK);
    assert(start() == ESP_OK);
    assert(fake_loops_created == 1);

    assert(subscribe(ESP_EVENT_ANY_ID, on_event, &all, nullptr) == ESP_OK);
    assert(subscribe(MOSAIC_EVENT_NOTIFICATION_RECEIVED, on_event, &notifications, nullptr) == ESP_OK);

    // Unchanged status is not posted.
    assert(set_status(Status::disconnected) == ESP_OK);
    assert(fake_posts == 0);

    // Changes are posted in order, and status() already reflects them in handlers.
    assert(set_status(Status::pairing) == ESP_OK);
    assert(set_status(Status::connected) == ESP_OK);
    assert(set_status(Status::connected) == ESP_OK);
    assert((all.statuses == std::vector { Status::pairing, Status::connected }));
    assert(all.status_matched_data);
    assert(notifications.statuses.empty());

    // A failed post leaves the old status, so retrying the same status posts again.
    fake_post_result = ESP_ERR_TIMEOUT;
    assert(set_status(Status::error) == ESP_ERR_TIMEOUT);
    assert(status() == Status::connected);
    assert(post_notification() == ESP_ERR_TIMEOUT);
    fake_post_result = ESP_OK;
    assert(set_status(Status::error) == ESP_OK);
    assert(all.statuses.back() == Status::error && status() == Status::error);

    // Every notification is posted.
    for (int i = 0; i < 3; ++i)
        assert(post_notification() == ESP_OK);
    assert(all.notifications == 3 && notifications.notifications == 3);
}
