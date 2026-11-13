#include "policy.hpp"

namespace mosaic::features::connection {

Action Policy::wifi_up(uint64_t now_ms) {
    wifi_ = true;
    // Wi-Fi coming back is worth trying straight away, without waiting out the backoff.
    if (phase_ == Phase::waiting)
        return start_attempt(now_ms);
    return Action::none;
}

Action Policy::wifi_down(uint64_t) {
    wifi_ = false;
    switch (phase_) {
    case Phase::opening:
    case Phase::replying:
    case Phase::pending:
    case Phase::paired:
        // The socket can't survive; close it now rather than wait for the pong timeout.
        phase_ = Phase::closing;
        return Action::close;
    case Phase::waiting:
    case Phase::closing:
    case Phase::stopped:
        break;
    }
    return Action::none;
}

Action Policy::opened(uint64_t now_ms) {
    if (phase_ != Phase::opening)
        return Action::none;
    phase_ = Phase::replying;
    deadline_ms_ = now_ms + kReplyTimeoutMs;
    return Action::send_connect;
}

Action Policy::closed(uint64_t now_ms, uint32_t random) {
    switch (phase_) {
    case Phase::waiting:
    case Phase::stopped:
        // Already handled, or a late report from an earlier socket.
        return Action::none;
    case Phase::opening:
    case Phase::replying:
    case Phase::pending:
    case Phase::paired:
    case Phase::closing:
        break;
    }
    const std::optional<Reject> rejection = rejection_;
    rejection_.reset();
    if (!rejection) {
        // A network failure: keeps the credential and retries with backoff (protocol section 5).
        fail(now_ms, random);
        state_ = backoff_.failures() >= kOfflineAfter ? State::offline : State::reconnecting;
        return Action::none;
    }
    switch (*rejection) {
    case Reject::unsupported_version:
        phase_ = Phase::stopped;
        state_ = State::update_required;
        return Action::none;
    case Reject::credential_revoked:
        // Reconnect straight away for a new pairing code, without backoff.
        backoff_.reset();
        phase_ = Phase::waiting;
        retry_at_ms_ = now_ms;
        state_ = State::reconnecting;
        return wifi_ ? start_attempt(now_ms) : Action::none;
    case Reject::malformed_request:
    case Reject::server_busy:
        break;
    }
    fail(now_ms, random);
    state_ = State::server_error;
    return Action::none;
}

Action Policy::received(uint64_t, Frame frame, Reject reason) {
    // Only the handshake replies matter here. Before paired, anything else is dropped without
    // an answer and the reply deadline keeps running (protocol section 3, item 4).
    if (phase_ != Phase::replying && phase_ != Phase::pending && phase_ != Phase::paired)
        return Action::none;
    switch (frame) {
    case Frame::paired:
        phase_ = Phase::paired;
        break;
    case Frame::pairing_pending:
        // A new code while paired would break the ordering; ignore it.
        if (phase_ == Phase::paired)
            return Action::none;
        phase_ = Phase::pending;
        break;
    case Frame::rejected:
        // The server closes after a rejection; close too, in case it doesn't.
        rejection_ = reason;
        phase_ = Phase::closing;
        return Action::close;
    case Frame::other:
        return Action::none;
    }
    backoff_.reset();
    state_ = State::online;
    return Action::none;
}

Action Policy::tick(uint64_t now_ms, uint32_t random) {
    switch (phase_) {
    case Phase::waiting:
        if (now_ms < retry_at_ms_)
            return Action::none;
        if (wifi_)
            return start_attempt(now_ms);
        // No Wi-Fi to try with: this retry fails at once, so a device without Wi-Fi still
        // reaches offline on the usual schedule.
        fail(now_ms, random);
        state_ = backoff_.failures() >= kOfflineAfter ? State::offline : state_;
        return Action::none;
    case Phase::opening:
    case Phase::replying:
        if (now_ms < deadline_ms_)
            return Action::none;
        phase_ = Phase::closing;
        return Action::close;
    case Phase::pending:
    case Phase::paired:
    case Phase::closing:
    case Phase::stopped:
        break;
    }
    return Action::none;
}

State Policy::state() const {
    return state_;
}

std::optional<uint64_t> Policy::next_deadline_ms() const {
    switch (phase_) {
    case Phase::waiting:
        return retry_at_ms_;
    case Phase::opening:
    case Phase::replying:
        return deadline_ms_;
    case Phase::pending:
    case Phase::paired:
    case Phase::closing:
    case Phase::stopped:
        break;
    }
    return std::nullopt;
}

bool Policy::application_frames_allowed() const {
    return phase_ == Phase::paired;
}

Action Policy::start_attempt(uint64_t now_ms) {
    phase_ = Phase::opening;
    deadline_ms_ = now_ms + kOpenTimeoutMs;
    rejection_.reset();
    return Action::open;
}

void Policy::fail(uint64_t now_ms, uint32_t random) {
    phase_ = Phase::waiting;
    retry_at_ms_ = now_ms + backoff_.next_delay_ms(random);
}

} // namespace mosaic::features::connection
