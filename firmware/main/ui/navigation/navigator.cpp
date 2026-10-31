#include "navigator.hpp"

namespace mosaic::ui::navigation {
namespace {
    Screen base(Conditions conditions) {
        if (conditions.starting)
            return Screen::boot;
        if (!conditions.online || !conditions.paired)
            return Screen::setup;
        return Screen::home;
    }

    // Provisional until #56: Settings opens from Home, and a notification from either.
    bool can_open(Screen from, Screen to) {
        switch (to) {
        case Screen::settings:
            return from == Screen::home;
        case Screen::notification:
            return from == Screen::home || from == Screen::settings;
        case Screen::boot:
        case Screen::setup:
        case Screen::home:
            return false; // Base screens follow the conditions, never a request.
        }
        return false;
    }
} // namespace

Screen Navigator::active() const {
    return stack_[size_ - 1];
}

void Navigator::update(Conditions conditions) {
    const Screen next = base(conditions);
    if (next == stack_[0])
        return;
    stack_[0] = next;
    size_ = 1;
}

bool Navigator::open(Screen screen) {
    if (size_ == kDepth || !can_open(active(), screen))
        return false;
    stack_[size_++] = screen;
    return true;
}

bool Navigator::back() {
    if (size_ == 1)
        return false;
    --size_;
    return true;
}

}
