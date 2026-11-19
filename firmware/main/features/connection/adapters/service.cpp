#include "service.hpp"
#include "credential_store.hpp"
#include "websocket.hpp"
#include "../credential.hpp"
#include "../frames.hpp"
#include "../policy.hpp"
#include "../../provisioning/adapters/settings_store.hpp"
#include "../../../events/bus.hpp"
#include "../../../support/secure_erase.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

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
    // Room for NVS writes of the credential, as well as the socket calls.
    constexpr uint32_t kTaskStack = 6 * 1'024;
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

    // The saved credential, once loaded or made; only the connection task touches it after
    // start(). Never log its secret.
    Credential s_credential { };
    bool s_have_credential = false;
    // The saved credential was paired at boot.
    bool s_restored_paired = false;

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
            if (s_restored_paired)
                policy_.restore_paired();
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
                if (event.attempt != attempt_)
                    return Action::none;
                remember(event.reply, now);
                return policy_.received(now, event.reply.frame, event.reply.reason);
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

        // A credential for `url`: the saved one if it belongs to it, otherwise a new one, saved
        // before it is ever sent. False if it can't be saved, and nothing may be sent.
        bool credential_for(std::string_view url) {
            if (s_have_credential && belongs_to(s_credential, url))
                return true;
            return replace_credential(url);
        }

        bool replace_credential(std::string_view url) {
            std::array<uint8_t, kSecretSize> secret { };
            esp_fill_random(secret.data(), secret.size());
            Credential made { };
            const bool fits = fresh(secret, url, made);
            support::secure_erase(secret);
            const esp_err_t err = fits ? credential_store::save(made) : ESP_ERR_INVALID_SIZE;
            support::secure_erase(s_credential);
            s_have_credential = err == ESP_OK;
            if (s_have_credential)
                s_credential = made;
            else
                ESP_LOGE(kTag, "Cannot save a device credential: %s", esp_err_to_name(err));
            support::secure_erase(made);
            return s_have_credential;
        }

        // Keeps the saved credential and the pairing code in step with what the server said.
        void remember(const Reply& reply, uint64_t now) {
            if (reply.frame == Frame::pairing_pending)
                events::set_pairing_code(events::make_pairing_code(reply.code.data(), reply.expires_in_s, now));
            else if (reply.frame == Frame::paired)
                events::set_pairing_code({ });
            switch (after(s_credential, reply)) {
            case Change::none:
                return;
            case Change::mark_paired:
            case Change::mark_unpaired: {
                s_credential.paired = reply.frame == Frame::paired;
                // If this fails, the next boot asks the server again, which knows the answer.
                const esp_err_t err = credential_store::save(s_credential);
                if (err != ESP_OK)
                    ESP_LOGW(kTag, "Cannot save the pairing: %s", esp_err_to_name(err));
                return;
            }
            case Change::replace: {
                ESP_LOGI(kTag, "Removed by the server; starting over with a new credential");
                events::set_pairing_code({ });
                // Same server, new secret. If saving fails, the next attempt makes one again
                // before sending anything.
                const auto url = s_credential.url;
                replace_credential(url.data());
                return;
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
            if (!credential_for(settings.ws_url)) {
                support::secure_erase(settings);
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
            const size_t length = encode_connect(s_credential.secret, frame);
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

    // Loads the saved credential before Wi-Fi starts, so a paired device starts paired.
    void restore() {
        provisioning::form::Settings settings { };
        bool have_settings = false;
        Credential saved { };
        bool have_saved = false;
        if (provisioning::adapters::settings::load(settings, have_settings) == ESP_OK && have_settings
            && credential_store::load(saved, have_saved) == ESP_OK && have_saved
            && belongs_to(saved, settings.ws_url)) {
            s_credential = saved;
            s_have_credential = true;
            s_restored_paired = saved.paired;
        }
        support::secure_erase(settings);
        support::secure_erase(saved);
        events::restore_paired(s_restored_paired);
        ESP_LOGI(kTag, "Device credential: %s", s_restored_paired ? "paired" : s_have_credential ? "unpaired" : "none");
    }

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
    restore();
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
