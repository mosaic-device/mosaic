#include "websocket.hpp"
#include "../frames.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"

namespace mosaic::features::connection::adapters::websocket {
namespace {
    constexpr char kTag[] = "connection";
    // Protocol section 6: ping every 15 s, and drop the connection after 15 s without a pong.
    constexpr int kPingIntervalSec = 15;
    constexpr int kPongTimeoutSec = 15;
    // Each connect, read or write step gives up after this; the policy's open deadline caps
    // the whole attempt.
    constexpr int kNetworkTimeoutMs = 10'000;
    // The client's default, plus room for decode_reply in Listener::frame.
    constexpr int kTaskStack = 6 * 1'024;
    constexpr TickType_t kSendTimeout = pdMS_TO_TICKS(5'000);
    constexpr TickType_t kCloseTimeout = pdMS_TO_TICKS(1'000);

    // RFC 6455 opcodes, as esp_websocket_client reports them.
    constexpr uint8_t kContinuation = 0x0;
    constexpr uint8_t kBinary = 0x2;

    esp_websocket_client_handle_t client = nullptr;
    Listener listener { };
    // Written by open() and close() on the service's task, read on the client's task.
    std::atomic<uint32_t> current { 0 };
    uint32_t last_attempt = 0;
    // Set on the client's task when the attempt ends; send() reads it.
    std::atomic<bool> ended { false };
    // Only touched on the client's task.
    std::array<uint8_t, kReceiveLimit> frame_buffer { };

    void end(uint32_t attempt) {
        if (!ended.exchange(true))
            listener.ended(attempt);
    }

    // Collects one binary frame, which the client may hand over in several pieces, and passes
    // it on once it is whole. Protocol section 6: anything the decoder can't take whole ends
    // the attempt instead of being cut short.
    void on_data(uint32_t attempt, const esp_websocket_event_data_t& data) {
        if (data.op_code == kContinuation || (data.op_code == kBinary && !data.fin)) {
            ESP_LOGW(kTag, "Server sent a fragmented message; closing");
            end(attempt);
            return;
        }
        // Text, ping, pong and close frames carry nothing for the device.
        if (data.op_code != kBinary)
            return;
        if (data.payload_len < 0 || static_cast<size_t>(data.payload_len) > frame_buffer.size()) {
            ESP_LOGW(kTag, "Server sent a frame over %u bytes; closing", static_cast<unsigned>(kReceiveLimit));
            end(attempt);
            return;
        }
        const auto offset = static_cast<size_t>(data.payload_offset);
        const auto length = static_cast<size_t>(data.data_len);
        const auto total = static_cast<size_t>(data.payload_len);
        if (data.payload_offset < 0 || data.data_len < 0 || offset + length > total) {
            end(attempt);
            return;
        }
        std::copy_n(reinterpret_cast<const uint8_t*>(data.data_ptr), length, frame_buffer.begin() + offset);
        if (offset + length == total)
            listener.frame(attempt, std::span<const uint8_t>(frame_buffer.data(), total));
    }

    void on_event(void* arg, esp_event_base_t, int32_t id, void* event_data) {
        const auto attempt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
        if (attempt != current.load() || ended.load())
            return;
        switch (id) {
        case WEBSOCKET_EVENT_CONNECTED:
            listener.opened(attempt);
            break;
        case WEBSOCKET_EVENT_DATA:
            on_data(attempt, *static_cast<const esp_websocket_event_data_t*>(event_data));
            break;
        // With reconnect off, every way a connection ends reaches one of these. TLS and
        // upgrade failures arrive as DISCONNECTED and never open setup.
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
        case WEBSOCKET_EVENT_FINISH:
            end(attempt);
            break;
        default:
            break;
        }
    }
} // namespace

uint32_t open(const char* url, const Listener& on) {
    close();
    // Protocol section 1: TLS only, never a plain ws:// fallback.
    if (!url || !std::string_view(url).starts_with("wss://"))
        return 0;
    listener = on;
    if (++last_attempt == 0)
        ++last_attempt;
    const uint32_t attempt = last_attempt;

    esp_websocket_client_config_t config { };
    config.uri = url;
    // The certificate bundle checks the chain, and the hostname is checked against it.
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;
    config.buffer_size = static_cast<int>(kReceiveLimit);
    config.disable_auto_reconnect = true;
    config.enable_close_reconnect = false;
    config.ping_interval_sec = kPingIntervalSec;
    config.pingpong_timeout_sec = kPongTimeoutSec;
    config.disable_pingpong_discon = false;
    config.network_timeout_ms = kNetworkTimeoutMs;
    config.task_stack = kTaskStack;
    config.task_name = "connection_ws";

    client = esp_websocket_client_init(&config);
    if (!client) {
        ESP_LOGW(kTag, "Cannot create the WebSocket client");
        return 0;
    }
    ended.store(false);
    current.store(attempt);
    void* arg = reinterpret_cast<void*>(static_cast<uintptr_t>(attempt));
    esp_err_t err = esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, on_event, arg);
    if (err == ESP_OK)
        err = esp_websocket_client_start(client);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Cannot start the WebSocket client: %s", esp_err_to_name(err));
        close();
        return 0;
    }
    return attempt;
}

bool send(std::span<const uint8_t> bytes) {
    if (!client || ended.load() || bytes.size() > kSendLimit || !esp_websocket_client_is_connected(client))
        return false;
    const int length = static_cast<int>(bytes.size());
    return esp_websocket_client_send_bin(client, reinterpret_cast<const char*>(bytes.data()), length, kSendTimeout)
        == length;
}

void close() {
    // From here on, whatever the old client reports is stale.
    current.store(0);
    if (!client)
        return;
    // A clean close frame when we can; destroy() stops the client's task either way.
    if (esp_websocket_client_is_connected(client))
        esp_websocket_client_close(client, kCloseTimeout);
    esp_websocket_client_destroy(client);
    client = nullptr;
}

} // namespace mosaic::features::connection::adapters::websocket
