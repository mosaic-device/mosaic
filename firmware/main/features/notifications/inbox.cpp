#include "inbox.hpp"

namespace mosaic::features::notifications {

Receipt Inbox::accept(const Notification& notification) {
    if (!valid(notification))
        return Receipt::invalid;
    if (holds(notification.id_view()))
        return Receipt::duplicate;
    items_[next_] = notification;
    next_ = (next_ + 1) % kCapacity;
    if (count_ < kCapacity)
        ++count_;
    ++arrivals_;
    return Receipt::stored;
}

const Notification* Inbox::latest() const {
    if (count_ == 0)
        return nullptr;
    return &items_[(next_ + kCapacity - 1) % kCapacity];
}

bool Inbox::holds(std::string_view id) const {
    for (size_t i = 0; i < count_; ++i)
        if (items_[(next_ + kCapacity - 1 - i) % kCapacity].id_view() == id)
            return true;
    return false;
}

} // namespace mosaic::features::notifications
