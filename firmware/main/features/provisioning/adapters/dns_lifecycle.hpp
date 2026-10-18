#pragma once

#include <atomic>
#include <cstdint>

namespace mosaic::features::provisioning::adapters::portal {
// Static storage in the adapter: a late worker never outlives its handshake.
// Lifecycle methods are owner-only; the worker only reads running/address and
// publishes completion as its final access, after closing its own socket.
class DnsLifecycle {
public:
    bool reap() {
        if (active_ && completed_.load(std::memory_order_acquire))
            active_ = false;
        return !active_;
    }
    bool begin(uint32_t address) {
        if (!reap())
            return false;
        address_ = address;
        completed_.store(false, std::memory_order_relaxed);
        running_.store(true, std::memory_order_relaxed);
        active_ = true;
        return true;
    }
    void request_stop() {
        running_.store(false);
    }
    bool running() const {
        return running_.load();
    }
    uint32_t address() const {
        return address_;
    }
    void complete() {
        completed_.store(true, std::memory_order_release);
    }
    // Only valid when task creation failed: no worker owns the socket/context.
    void creation_failed() {
        request_stop();
        complete();
        reap();
    }

private:
    bool active_ = false;
    uint32_t address_ = 0;
    std::atomic_bool running_ { false };
    std::atomic_bool completed_ { false };
};
} // namespace mosaic::features::provisioning::adapters::portal
