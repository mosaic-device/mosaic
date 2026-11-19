#include "service.hpp"
#include "websocket.hpp"
#include "../frames.hpp"
#include "../policy.hpp"
#include "../../provisioning/adapters/settings_store.hpp"
#include "../../../events/bus.hpp"
#include "../../../support/secure_erase.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_types.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"
#include "freertos/task.h"

namespace mosaic::features::connection::adapters::service {
namespace {
    constexpr char kTag[] = "connection";
    constexpr uint32_t kTaskStack = 4 * 1'024;
    constexpr UBaseType_t kTaskPriority = 3;
    constexpr UBaseType_t kQueueLength = 8;
    // The task wakes at least this often, so a lost queue slot only delays an event.
    constexpr uint64_t kMaxWaitMs = 1'000;

    enum class Kind : uint8_t { wifi_up, wifi_down, opened, ended, frame };
    struct Event {
        Kind kind;
        uint32_t attempt = 0;
        Reply reply { };
    };

    QueueHandle_t s_queue = nullptr;
    // An ended report that didn't fit in the queue; the task picks it up on its next wake.
    std::atomic<uint32_t> s_unqueued_end { 0 };

    // Placeholder until #111: a fresh secret each boot, never stored or logged, so the server
    // sees a new unpaired device after every reboot. #111 replaces it with the stored device
    // credential.
    std::array<uint8_t, kSecretSize> s_secret { };

    uint64_t now_ms() {
        return static_cast<uint64_t>(esp_timer_get_time()) / 1'000;
    }

    // Never blocks: the WebSocket client's task posts here, and close() waits for that task.
    bool post(const Event& event) {
        return xQueueSend(s_queue, &event, 0) == pdTRUE;
    }

    void on_opened(uint32_t attempt) {
        post({ .kind = Kind::opened, .attempt = attempt });
    }
    void on_ended(uint32_t attempt) {
        if (!post({ .kind = Kind::ended, .attempt = attempt }))
            s_unqueued_end.store(attempt);
    }
    void on_frame(uint32_t attempt, std::span<const uint8_t> bytes) {
        // A dropped reply only lets the reply deadline run out, and the policy retries.
        post({ .kind = Kind::frame, .attempt = attempt, .reply = decode_reply(bytes) });
    }
    constexpr websocket::Listener kListener { .opened = on_opened, .ended = on_ended, .frame = on_frame };

    void on_network(void*, esp_event_base_t base, int32_t id, void*) {
        if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
            post({ .kind = Kind::wifi_up });
        else
            post({ .kind = Kind::wifi_down });
    }

    const char* name(State state) {
        switch (state) {
        case State::reconnecting:
            return "reconnecting";
        case State::online:
            return "online";
        case State::offline:
            return "offline";
        case State::server_error:
            return "server error";
        case State::update_required:
            return "update required";
        }
        return "unknown";
    }

    class Connection {
    public:
        void run() {
            State shown = policy_.state();
            ESP_LOGI(kTag, "Server connection: %s", name(shown));
            while (true) {
                Event event { };
                const bool received = xQueueReceive(s_queue, &event, wait()) == pdTRUE;
                if (received)
                    carry_out(handle(event));
                if (const uint32_t lost = s_unqueued_end.exchange(0))
                    carry_out(handle({ .kind = Kind::ended, .attempt = lost }));
                const auto deadline = policy_.next_deadline_ms();
                if (deadline && now_ms() >= *deadline)
                    carry_out(policy_.tick(now_ms(), esp_random()));
                if (policy_.state() != shown) {
                    shown = policy_.state();
                    ESP_LOGI(kTag, "Server connection: %s", name(shown));
                }
                publish();
            }
        }

    private:
        // Only on a change, so a debug trigger's status holds until the connection moves on.
        // A post that fails is tried again on the next wake.
        void publish() {
            const events::Status status = policy_.status();
            if (status != published_ && events::set_status(status) == ESP_OK)
                published_ = status;
        }

        TickType_t wait() const {
            const uint64_t now = now_ms();
            uint64_t ms = kMaxWaitMs;
            if (const auto deadline = policy_.next_deadline_ms())
                ms = *deadline > now ? std::min(*deadline - now, kMaxWaitMs) : 0;
            return pdMS_TO_TICKS(ms);
        }

        Action handle(const Event& event) {
            const uint64_t now = now_ms();
            switch (event.kind) {
            case Kind::wifi_up:
                return policy_.wifi_up(now);
            case Kind::wifi_down:
                return policy_.wifi_down(now);
            case Kind::opened:
                return event.attempt == attempt_ ? policy_.opened(now) : Action::none;
            case Kind::ended:
                return event.attempt == attempt_ ? closed() : Action::none;
            case Kind::frame:
                return event.attempt == attempt_ ? policy_.received(now, event.reply.frame, event.reply.reason)
                                                 : Action::none;
            }
            return Action::none;
        }

        // Each action can lead to another, such as a failed open reporting closed().
        void carry_out(Action action) {
            while (action != Action::none) {
                switch (action) {
                case Action::open:
                    action = open();
                    break;
                case Action::send_connect:
                    action = send_connect();
                    break;
                case Action::close:
                    action = closed();
                    break;
                case Action::none:
                    break;
                }
            }
        }

        Action open() {
            provisioning::form::Settings settings { };
            bool found = false;
            const esp_err_t err = provisioning::adapters::settings::load(settings, found);
            if (err != ESP_OK || !found) {
                support::secure_erase(settings);
                ESP_LOGW(kTag, "No saved server address (%s)", err == ESP_OK ? "not set" : esp_err_to_name(err));
                return policy_.closed(now_ms(), esp_random());
            }
            // The URL exactly as the owner entered it; the adapter refuses anything but wss://.
            attempt_ = websocket::open(settings.ws_url, kListener);
            support::secure_erase(settings);
            if (attempt_ == 0) {
                ESP_LOGW(kTag, "Cannot start a connection to the saved server address");
                return policy_.closed(now_ms(), esp_random());
            }
            return Action::none;
        }

        Action send_connect() {
            std::array<uint8_t, kSendLimit> frame { };
            const size_t length = encode_connect(s_secret, frame);
            const bool sent = length > 0 && websocket::send(std::span<const uint8_t>(frame.data(), length));
            support::secure_erase(frame);
            if (sent)
                return Action::none;
            ESP_LOGW(kTag, "Cannot send connect");
            return closed();
        }

        // Frees the current attempt, whatever ended it, and lets the policy decide what's next.
        Action closed() {
            websocket::close();
            attempt_ = 0;
            return policy_.closed(now_ms(), esp_random());
        }

        Policy policy_;
        uint32_t attempt_ = 0;
        // What the bus starts with.
        events::Status published_ = events::Status::disconnected;
    };

    void run(void*) {
        static Connection connection;
        connection.run();
    }
} // namespace

esp_err_t start() {
    if (s_queue)
        return ESP_OK;
    s_queue = xQueueCreate(kQueueLength, sizeof(Event));
    if (!s_queue)
        return ESP_ERR_NO_MEM;
    esp_fill_random(s_secret.data(), s_secret.size());
    esp_err_t err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_network, nullptr, nullptr);
    if (err == ESP_OK)
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_network, nullptr, nullptr);
    if (err == ESP_OK)
        err = esp_event_handler_instance_register(
            WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_network, nullptr, nullptr);
    if (err != ESP_OK)
        return err;
    if (xTaskCreate(run, "connection", kTaskStack, nullptr, kTaskPriority, nullptr) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

}
