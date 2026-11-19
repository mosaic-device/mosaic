#pragma once

#include "notification.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mosaic::features::notifications {

// The most recent notifications, in RAM: enough to tell a resend from a new one and to show
// what arrived. Not kept across a restart. Not thread-safe; the store adapter guards it.
class Inbox {
public:
    static constexpr size_t kCapacity = 16;

    // Checks and keeps a notification. The oldest one makes room once the inbox is full.
    Receipt accept(const Notification& notification);
    // The newest notification, or null when none has arrived.
    const Notification* latest() const;
    // Counts every notification stored, so a reader can tell a new one arrived.
    uint32_t arrivals() const {
        return arrivals_;
    }

private:
    bool holds(std::string_view id) const;

    std::array<Notification, kCapacity> items_ { };
    size_t count_ = 0;
    // Where the next one goes; the newest is just before it.
    size_t next_ = 0;
    uint32_t arrivals_ = 0;
};

} // namespace mosaic::features::notifications
