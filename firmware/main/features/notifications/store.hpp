#pragma once

#include "notification.hpp"

#include <cstdint>

// The device's notifications: one inbox shared by the connection, which adds to it, and the
// screens, which show the newest. Any task.
namespace mosaic::features::notifications::store {

// Checks and keeps a notification; see Inbox::accept(). The caller confirms it to the server
// and then announces a stored one with events::post_notification().
Receipt accept(const Notification& notification);

// Copies the newest notification to `out`. False when none has arrived. `arrivals` counts
// every one stored, so a reader can tell when a new one came in.
bool latest(Notification& out, uint32_t& arrivals);

} // namespace mosaic::features::notifications::store
