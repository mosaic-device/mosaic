#pragma once

#include "backoff.hpp"

#include <cstdint>
#include <optional>

namespace mosaic::features::connection {

// What the device shows about its server connection; the Connection track of the wiki's
// State Map. Nothing here ever opens setup or clears saved settings: the owner corrects a
// wrong address with the two-tap setup recovery.
enum class State : uint8_t {
    // Trying to connect, or waiting to retry, after fewer than kOfflineAfter failures.
    reconnecting,
    // The server answered connect with paired or pairing_pending.
    online,
    // kOfflineAfter or more failures in a row. Retries continue.
    offline,
    // The server rejected the device as malformed or busy. Retries continue.
    server_error,
    // The server doesn't support this firmware's protocol version. No retries until reboot.
    update_required,
};

// Show offline after this many failed attempts in a row (protocol section 6).
inline constexpr uint32_t kOfflineAfter = 5;
// connect must be answered within this long (protocol section 3, item 2).
inline constexpr uint64_t kReplyTimeoutMs = 10'000;
// The socket adapter reports every failure itself; this only stops a stuck open from
// stalling retries for good.
inline constexpr uint64_t kOpenTimeoutMs = 30'000;

// The server's reply frames, decoded. Anything else, including an empty envelope, is `other`.
enum class Frame : uint8_t { paired, pairing_pending, rejected, other };

// Why the server rejected the device. The decoder maps an unspecified or unknown reason to
// server_busy, as protocol section 4 requires.
enum class Reject : uint8_t { malformed_request, unsupported_version, credential_revoked, server_busy };

// What the service should do next. The service reports how it went with opened() or closed().
enum class Action : uint8_t { none, open, send_connect, close };

// Decides when to connect, retry and give up, and what State to show. Pure logic: the
// service feeds it Wi-Fi, socket and frame events with the current monotonic time, and
// calls tick() at next_deadline_ms().
class Policy {
public:
    Action wifi_up(uint64_t now_ms);
    Action wifi_down(uint64_t now_ms);
    // The secure WebSocket is open: send connect and start the reply deadline.
    Action opened(uint64_t now_ms);
    // The socket closed or failed to open, whatever the cause: TLS or certificate failure,
    // failed upgrade, Wi-Fi loss, a missed pong, or the close after a rejection.
    // `random` jitters the retry wait.
    Action closed(uint64_t now_ms, uint32_t random);
    // A decoded frame. For Frame::rejected, `reason` says why; it is ignored otherwise.
    Action received(uint64_t now_ms, Frame frame, Reject reason = Reject::server_busy);
    // Starts a due retry or ends an attempt whose deadline passed.
    Action tick(uint64_t now_ms, uint32_t random);

    State state() const;
    // When tick() next has something to do, or nothing if it is waiting on an event.
    std::optional<uint64_t> next_deadline_ms() const;
    // Whether application frames such as notifications may be sent and received: only once
    // the server said paired on this connection.
    bool application_frames_allowed() const;

private:
    enum class Phase : uint8_t {
        waiting,  // For retry_at_ms_, with no socket.
        opening,  // Asked the service to open; waiting for opened() or closed().
        replying, // Sent connect; waiting for the reply.
        pending,  // pairing_pending received.
        paired,   // paired received.
        closing,  // Asked the service to close; waiting for closed().
        stopped,  // Update required: no more attempts this boot.
    };

    Action start_attempt(uint64_t now_ms);
    void fail(uint64_t now_ms, uint32_t random);

    Phase phase_ = Phase::waiting;
    State state_ = State::reconnecting;
    Backoff backoff_;
    bool wifi_ = false;
    // Due now: a booting device tries as soon as Wi-Fi is up.
    uint64_t retry_at_ms_ = 0;
    uint64_t deadline_ms_ = 0;
    // The rejection received on this connection, acted on once it closes.
    std::optional<Reject> rejection_;
};

} // namespace mosaic::features::connection
