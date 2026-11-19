#pragma once

#include <cstdint>

namespace mosaic::features::notifications {

// What the notification screen shows, from the State Map: a new notification opens the
// incoming view; View opens its detail; Not now, Back and a timeout close it. Times are
// milliseconds from one clock and may wrap around.
class Flow {
public:
    // The incoming view closes by itself after this long untouched.
    static constexpr uint32_t kIncomingMs = 30'000;

    enum class View : uint8_t { none, incoming, detail };

    View view() const {
        return view_;
    }
    // A notification was stored. The newest always shows, replacing whatever was open.
    void arrived(uint32_t now_ms) {
        view_ = View::incoming;
        since_ms_ = now_ms;
    }
    // View: the incoming notification's detail. False anywhere else.
    bool open_detail() {
        if (view_ != View::incoming)
            return false;
        view_ = View::detail;
        return true;
    }
    // Not now, Back, or leaving the screen any other way.
    void dismiss() {
        view_ = View::none;
    }
    // Call every tick. True once, when the incoming view has waited kIncomingMs and closes.
    // The detail view stays until the owner leaves it.
    bool tick(uint32_t now_ms) {
        if (view_ != View::incoming || now_ms - since_ms_ < kIncomingMs)
            return false;
        view_ = View::none;
        return true;
    }

private:
    View view_ = View::none;
    uint32_t since_ms_ = 0;
};

} // namespace mosaic::features::notifications
