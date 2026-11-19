#pragma once

#include <cstdint>
#include <span>

// The secure WebSocket to the server: one connection at a time over esp_websocket_client.
// It only moves bytes. Retries, deadlines and what frames mean belong to the policy, so the
// client's own reconnect is off.
namespace mosaic::features::connection::adapters::websocket {

// Called on the client's task. Each call names the attempt it belongs to, so the service can
// drop events that were queued before it closed that attempt.
struct Listener {
    // The TLS and WebSocket upgrade succeeded; send connect.
    void (*opened)(uint32_t attempt);
    // The attempt ended: it failed to open, the server closed it, a pong was missed, or a
    // frame broke the limits. Reported at most once per attempt. The service must still call
    // close() to free it.
    void (*ended)(uint32_t attempt);
    // One whole binary frame of at most kReceiveLimit bytes, valid only during the call.
    // The task's stack has room for decode_reply. Never log it.
    void (*frame)(uint32_t attempt, std::span<const uint8_t> bytes);
};

// Starts connecting to a wss:// URL and returns at once with the attempt's number, or 0 if
// the client couldn't start; that attempt reports nothing, so treat it as closed. Call only
// when no attempt is open. The URL is copied.
uint32_t open(const char* url, const Listener& listener);

// Sends one binary frame on the open attempt. Never log it.
bool send(std::span<const uint8_t> bytes);

// Ends the current attempt and frees it, waiting for the client's task to stop. Events the
// attempt reports from now on are stale. Safe to repeat. Never call it from a Listener.
void close();

} // namespace mosaic::features::connection::adapters::websocket
