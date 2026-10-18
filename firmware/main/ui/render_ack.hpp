#pragma once

#include <atomic>
#include <cstdint>

namespace mosaic::ui {

// One synchronous render waiter, one LVGL worker. Times are monotonic milliseconds;
// signed subtraction is safe across wrap for deadlines less than 2^31 ms away.
// Queue acceptance is deliberately not represented as a refresh acknowledgement.
class RenderAck {
public:
    explicit RenderAck(uint32_t initial_token = 0)
        : next_token_(initial_token) { }
    uint32_t begin(uint32_t deadline_ms) {
        uint32_t token = ++next_token_;
        if (!token)
            token = ++next_token_;
        deadline_ms_.store(deadline_ms);
        // Keep the previous completion while LVGL releases its prior render.
        if (ack_token_.load() == token)
            ack_token_.store(0);
        if (completed_token_.load() == token)
            completed_token_.store(0);
        flushed_token_.store(0);
        active_token_.store(token);
        return token;
    }

    bool current(uint32_t token, uint32_t now_ms) const {
        return token && active_token_.load() == token && static_cast<int32_t>(deadline_ms_.load() - now_ms) > 0;
    }

    void note_flush(uint32_t token, uint32_t now_ms) {
        if (current(token, now_ms))
            flushed_token_.store(token);
    }

    void acknowledge_refresh(uint32_t token, uint32_t now_ms) {
        if (current(token, now_ms) && flushed_token_.load() == token)
            ack_token_.store(token);
    }

    bool acknowledged(uint32_t token, uint32_t now_ms) const {
        return current(token, now_ms) && ack_token_.load() == token;
    }

    bool complete(uint32_t token, uint32_t now_ms) {
        if (!acknowledged(token, now_ms))
            return false;
        completed_token_.store(token);
        active_token_.store(0);
        return true;
    }

    void cancel(uint32_t token) {
        if (active_token_.load() != token || !token)
            return;
        active_token_.store(0);
        if (flushed_token_.load() == token)
            flushed_token_.store(0);
        if (ack_token_.load() == token)
            ack_token_.store(0);
    }

    // A flush/refresh acknowledgement is provisional until the waiter commits it.
    bool completed(uint32_t token) const {
        return token && completed_token_.load() == token;
    }

    // Retain the displayed-frame barrier until the waiter commits or cancels,
    // even if its deadline passes while that task is descheduled.
    bool pending(uint32_t token) const {
        return token && active_token_.load() == token;
    }

private:
    uint32_t next_token_ = 0; // single render caller at a time
    std::atomic_uint32_t active_token_ { 0 };
    std::atomic_uint32_t flushed_token_ { 0 };
    std::atomic_uint32_t ack_token_ { 0 };
    std::atomic_uint32_t completed_token_ { 0 };
    std::atomic_uint32_t deadline_ms_ { 0 };
};

} // namespace mosaic::ui
