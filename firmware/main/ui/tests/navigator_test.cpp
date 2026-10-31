#include "../navigation/navigator.hpp"

#include <cassert>
#include <initializer_list>

using namespace mosaic::ui::navigation;

namespace {
constexpr Conditions kStarting { };
constexpr Conditions kOffline { .starting = false, .online = false, .paired = false };
constexpr Conditions kUnpaired { .starting = false, .online = true, .paired = false };
constexpr Conditions kPaired { .starting = false, .online = true, .paired = true };
constexpr Conditions kPairedOffline { .starting = false, .online = false, .paired = true };

constexpr std::initializer_list<Screen> kAll {
    Screen::boot,
    Screen::setup,
    Screen::home,
    Screen::settings,
    Screen::notification,
};

Navigator at_home() {
    Navigator navigator;
    navigator.update(kPaired);
    return navigator;
}

void conditions_choose_the_base_screen() {
    Navigator navigator;
    assert(navigator.active() == Screen::boot);
    navigator.update(kStarting);
    assert(navigator.active() == Screen::boot);
    navigator.update(kOffline);
    assert(navigator.active() == Screen::setup);
    // Home waits for pairing; Wi-Fi alone shows the unpaired setup screen.
    navigator.update(kUnpaired);
    assert(navigator.active() == Screen::setup);
    navigator.update(kPaired);
    assert(navigator.active() == Screen::home);
    // Losing Wi-Fi returns to provisioning's screens even when paired.
    navigator.update(kPairedOffline);
    assert(navigator.active() == Screen::setup);
}

void settings_opens_from_home_and_back_returns() {
    Navigator navigator = at_home();
    assert(navigator.open(Screen::settings));
    assert(navigator.active() == Screen::settings);
    assert(navigator.back());
    assert(navigator.active() == Screen::home);
}

void back_does_nothing_on_base_screens() {
    for (Conditions conditions : { kStarting, kOffline, kUnpaired, kPaired }) {
        Navigator navigator;
        navigator.update(conditions);
        const Screen before = navigator.active();
        assert(!navigator.back());
        assert(navigator.active() == before);
    }
}

void unavailable_screens_do_not_open() {
    // Nothing opens from boot or setup: provisioning keeps the screen until it is done.
    for (Conditions conditions : { kStarting, kOffline, kUnpaired }) {
        Navigator navigator;
        navigator.update(conditions);
        const Screen before = navigator.active();
        for (Screen screen : kAll) {
            assert(!navigator.open(screen));
            assert(navigator.active() == before);
        }
    }
    // Base screens are never opened by request, and Settings does not stack on itself.
    Navigator navigator = at_home();
    for (Screen screen : { Screen::boot, Screen::setup, Screen::home })
        assert(!navigator.open(screen));
    assert(navigator.open(Screen::settings));
    assert(!navigator.open(Screen::settings));
    assert(navigator.active() == Screen::settings);
}

void notification_returns_to_what_was_underneath() {
    Navigator navigator = at_home();
    assert(navigator.open(Screen::notification));
    assert(!navigator.open(Screen::settings));
    assert(navigator.back());
    assert(navigator.active() == Screen::home);

    assert(navigator.open(Screen::settings));
    assert(navigator.open(Screen::notification));
    assert(navigator.back());
    assert(navigator.active() == Screen::settings);
    assert(navigator.back());
    assert(navigator.active() == Screen::home);
    assert(!navigator.back());
}

void a_new_base_closes_opened_screens() {
    Navigator navigator = at_home();
    assert(navigator.open(Screen::settings));
    assert(navigator.open(Screen::notification));
    // Unchanged conditions keep what is open.
    navigator.update(kPaired);
    assert(navigator.active() == Screen::notification);
    // Losing Wi-Fi shows provisioning; coming back starts at Home, not the old stack.
    navigator.update(kPairedOffline);
    assert(navigator.active() == Screen::setup);
    assert(!navigator.back());
    navigator.update(kPaired);
    assert(navigator.active() == Screen::home);
    assert(!navigator.back());
}

void every_reachable_screen_has_a_way_back() {
    // From any screen opened on Home, repeated Back always reaches Home.
    for (Screen first : kAll) {
        for (Screen second : kAll) {
            Navigator navigator = at_home();
            navigator.open(first);
            navigator.open(second);
            int steps = 0;
            while (navigator.back())
                ++steps;
            assert(steps <= 2);
            assert(navigator.active() == Screen::home);
        }
    }
}
} // namespace

int main() {
    conditions_choose_the_base_screen();
    settings_opens_from_home_and_back_returns();
    back_does_nothing_on_base_screens();
    unavailable_screens_do_not_open();
    notification_returns_to_what_was_underneath();
    a_new_base_closes_opened_screens();
    every_reachable_screen_has_a_way_back();
}
