#include "store.hpp"
#include "inbox.hpp"

#include <mutex>

namespace mosaic::features::notifications::store {
namespace {
    std::mutex s_lock;
    Inbox s_inbox;
} // namespace

Receipt accept(const Notification& notification) {
    const std::lock_guard lock(s_lock);
    return s_inbox.accept(notification);
}

bool latest(Notification& out, uint32_t& arrivals) {
    const std::lock_guard lock(s_lock);
    arrivals = s_inbox.arrivals();
    const Notification* newest = s_inbox.latest();
    if (!newest)
        return false;
    out = *newest;
    return true;
}

} // namespace mosaic::features::notifications::store
