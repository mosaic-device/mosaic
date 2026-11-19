#include "../navigation/navigator.hpp"
#include "../navigation/paired_hold.hpp"

#include <cassert>
#include <initializer_list>

using namespace mosaic::ui::navigation;

namespace {
constexpr Conditions kStarting { };
constexpr Conditions kSetup { .starting = false, .setup = true, .paired = false };
constexpr Conditions kUnpaired { .starting = false, .setup = false, .paired = false };
constexpr Conditions kPaired { .starting = false, .setup = false, .paired = true };
constexpr Conditions kPairedSetup { .starting = false, .setup = true, .paired = true };

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
    navigator.update(kSetup);
    assert(navigator.active() == Screen::setup);
    // Home waits for pairing: provisioning's screens show Wi-Fi, the server and the code.
    navigator.update(kUnpaired);
    assert(navigator.active() == Screen::setup);
    // Once paired, Home, whatever Wi-Fi or the server are doing.
    navigator.update(kPaired);
    assert(navigator.active() == Screen::home);
    // Only setup itself takes over a paired device.
    navigator.update(kPairedSetup);
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
    for (Conditions conditions : { kStarting, kSetup, kUnpaired, kPaired }) {
        Navigator navigator;
        navigator.update(conditions);
        const Screen before = navigator.active();
        assert(!navigator.back());
        assert(navigator.active() == before);
    }
}

void unavailable_screens_do_not_open() {
    // Nothing opens from boot or setup: provisioning keeps the screen until it is done.
    for (Conditions conditions : { kStarting, kSetup, kUnpaired, kPairedSetup }) {
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
    // Opening setup shows provisioning; leaving it starts at Home, not the old stack.
    navigator.update(kPairedSetup);
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

void pairing_holds_home_back_briefly() {
    // Paired at boot: Home at once.
    PairedHold restored;
    assert(restored.paired(true, 0) && !restored.holding());
    // Paired while waiting: the pairing screen says so first.
    PairedHold hold;
    assert(!hold.paired(false, 0));
    assert(!hold.paired(true, 1'000) && hold.holding());
    assert(!hold.paired(true, 1'000 + PairedHold::kHoldMs - 1));
    assert(hold.paired(true, 1'000 + PairedHold::kHoldMs) && !hold.holding());
    assert(hold.paired(true, 50'000));
    // Unpaired during the hold: nothing to celebrate.
    PairedHold removed;
    removed.paired(false, 0);
    removed.paired(true, 100);
    assert(!removed.paired(false, 200) && !removed.holding());
    // The clock wrapping around doesn't cut it short.
    PairedHold wrapping;
    wrapping.paired(false, 0xFFFF'FF00);
    assert(!wrapping.paired(true, 0xFFFF'FF00));
    assert(!wrapping.paired(true, 0x0000'0100));
    assert(wrapping.paired(true, 0xFFFF'FF00 + PairedHold::kHoldMs));
}
} // namespace

int main() {
    pairing_holds_home_back_briefly();
    conditions_choose_the_base_screen();
    settings_opens_from_home_and_back_returns();
    back_does_nothing_on_base_screens();
    unavailable_screens_do_not_open();
    notification_returns_to_what_was_underneath();
    a_new_base_closes_opened_screens();
    every_reachable_screen_has_a_way_back();
}
