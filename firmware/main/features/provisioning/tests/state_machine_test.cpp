#include "../state_machine.hpp"

#include <cassert>
#include <cstring>
#include <initializer_list>

using namespace mosaic::features::provisioning;

namespace {
form::Settings credentials() {
    form::Settings value;
    std::strcpy(value.ssid, "Home");
    std::strcpy(value.pass, "password");
    std::strcpy(value.ws_url, "wss://example.org/device");
    return value;
}

void expect(const Result& result, std::initializer_list<EffectType> types) {
    assert(result.count <= result.effects.size());
    assert(result.count <= 1); // current branches emit at most one consequential effect
    assert(result.count == types.size());
    size_t i = 0;
    for (auto type : types)
        assert(result.effects[i++].type == type);
}

void setup(StateMachine& model) {
    expect(model.dispatch(Start { false }, 0), { EffectType::open_portal });
    assert(model.snapshot().phase == Phase::opening_portal);
    expect(model.dispatch(StartupCompleted { true }, 1), { });
    assert(model.snapshot().portal_status == PortalStatus::ready);
    assert(model.snapshot().portal_active);
}

void test_boot_selection() {
    for (bool has_saved : { false, true }) {
        for (bool requested : { false, true }) {
            StateMachine model;
            auto result = model.dispatch(Start { has_saved, requested }, 0);
            if (has_saved && !requested) {
                expect(result, { EffectType::start_saved_connection });
                assert(model.snapshot().phase == Phase::starting_saved);
            } else {
                expect(result, { EffectType::open_portal });
                assert(model.snapshot().phase == Phase::opening_portal);
            }
            assert(model.snapshot().has_saved == has_saved);
            expect(model.dispatch(Start { !has_saved, !requested }, 1), { });
            expect(model.dispatch(StartupCompleted { true }, 2), { });
            assert(model.snapshot().portal_active == (!has_saved || requested));
        }
    }
    StateMachine saved_failed;
    expect(saved_failed.dispatch(Start { true }, 0), { EffectType::start_saved_connection });
    expect(saved_failed.dispatch(StartupCompleted { false }, 1), { });
    expect(saved_failed.dispatch(Connected { }, 2), { });
    expect(saved_failed.dispatch(GotIp { }, 3), { });
    expect(saved_failed.dispatch(Tick { }, 100'000'000), { });
    assert(saved_failed.snapshot().phase == Phase::startup_failed && !saved_failed.snapshot().online);
    StateMachine failed;
    expect(failed.dispatch(Start { false }, 0), { EffectType::open_portal });
    expect(failed.dispatch(StartupCompleted { false }, 1), { });
    expect(failed.dispatch(SetupTapped { }, 2), { });
    expect(failed.dispatch(Tick { }, 100'000'000), { });
    assert(failed.snapshot().phase == Phase::startup_failed);
}

void test_save_and_reboot() {
    for (bool has_saved : { false, true }) {
        StateMachine model;
        expect(model.dispatch(Start { has_saved, true }, 0), { EffectType::open_portal });
        expect(model.dispatch(StartupCompleted { true }, 1), { });
        auto result = model.dispatch(Submit { credentials() }, 2);
        expect(result, { EffectType::persist_candidate });
        assert(std::strcmp(result.effects[0].settings.pass, "password") == 0);
        assert(model.snapshot().phase == Phase::saving);
        assert(model.snapshot().portal_status == PortalStatus::saving);
        expect(model.dispatch(Submit { credentials() }, 3), { });
        expect(model.dispatch(Connected { }, 4), { });
        expect(model.dispatch(GotIp { }, 5), { });
        expect(model.dispatch(Disconnected { }, 6), { });
        expect(model.dispatch(WifiObservationLost { }, 7), { });
        expect(model.dispatch(Tick { }, 100'000'000), { });
        assert(model.snapshot().phase == Phase::saving && !model.snapshot().online);
        expect(model.dispatch(Saved { false }, 8), { });
        assert(model.snapshot().portal_status == PortalStatus::storage_error);
        assert(model.snapshot().has_saved == has_saved);
        expect(model.dispatch(Submit { credentials() }, 9), { EffectType::persist_candidate });
        expect(model.dispatch(Saved { true }, 10), { });
        assert(model.snapshot().phase == Phase::saved_visible);
        assert(model.snapshot().has_saved && !model.snapshot().online && model.snapshot().portal_active);
        assert(model.snapshot().portal_status == PortalStatus::saved);
        expect(model.dispatch(Saved { true }, 11), { });
        expect(model.dispatch(Submit { credentials() }, 12), { });
        expect(model.dispatch(SetupTapped { }, 13), { });
        expect(model.dispatch(GotIp { }, 14), { });
        expect(model.dispatch(WifiObservationLost { }, 15), { });
        expect(model.dispatch(Tick { }, 6'000'009), { });
        expect(model.dispatch(Tick { }, 6'000'010), { EffectType::reboot });
        assert(model.snapshot().phase == Phase::rebooting && !model.snapshot().online);
        expect(model.dispatch(Tick { }, 6'000'011), { });
        expect(model.dispatch(SetupTapped { }, 6'000'012), { });
        expect(model.dispatch(SetupDisplayFailed { }, 6'000'013), { });
    }
    // Losing the display during the saved message cancels its pending reboot.
    StateMachine display_lost;
    setup(display_lost);
    expect(display_lost.dispatch(Submit { credentials() }, 2), { EffectType::persist_candidate });
    expect(display_lost.dispatch(Saved { true }, 3), { });
    expect(display_lost.dispatch(SetupDisplayFailed { }, 4), { EffectType::stop_portal_and_wifi });
    expect(display_lost.dispatch(Tick { }, 6'000'003), { });
    assert(display_lost.snapshot().phase == Phase::recovery_failed);
    expect(display_lost.dispatch(SetupTapped { }, 6'000'004), { });
    expect(display_lost.dispatch(SetupTapped { }, 6'000'004), { EffectType::request_setup_reboot });
    StateMachine custom(Timing { .success_visible = 7 });
    setup(custom);
    expect(custom.dispatch(Submit { credentials() }, 2), { EffectType::persist_candidate });
    expect(custom.dispatch(Saved { true }, 3), { });
    expect(custom.dispatch(Tick { }, 9), { });
    expect(custom.dispatch(Tick { }, 10), { EffectType::reboot });
}

void test_display_failure() {
    for (bool has_saved : { false, true }) {
        for (bool saving : { false, true }) {
            StateMachine model;
            expect(model.dispatch(Start { has_saved, true }, 0), { EffectType::open_portal });
            expect(model.dispatch(StartupCompleted { true }, 1), { });
            if (saving)
                expect(model.dispatch(Submit { credentials() }, 2), { EffectType::persist_candidate });
            expect(model.dispatch(SetupDisplayFailed { }, 3), { EffectType::stop_portal_and_wifi });
            assert(model.snapshot().phase == Phase::recovery_failed);
            assert(!model.snapshot().portal_active && !model.snapshot().online);
            expect(model.dispatch(Saved { true }, 4), { });
            expect(model.dispatch(GotIp { }, 5), { });
            expect(model.dispatch(Tick { }, 100'000'000), { }); // no AP-only STA fallback
            expect(model.dispatch(SetupTapped { }, 100'000'001), { });
            expect(model.dispatch(SetupTapped { }, 100'000'001), { EffectType::request_setup_reboot });
            expect(model.dispatch(SetupRequested { true }, 100'000'002), { EffectType::reboot });
        }
    }
}

void test_recovery_request() {
    StateMachine model;
    expect(model.dispatch(Start { true }, 0), { EffectType::start_saved_connection });
    expect(model.dispatch(StartupCompleted { true }, 1), { });
    expect(model.dispatch(Connected { }, 2), { });
    expect(model.dispatch(GotIp { }, 3), { });
    expect(model.dispatch(SetupRequested { true }, 4), { });
    expect(model.dispatch(SetupTapped { }, 5), { });
    expect(model.dispatch(SetupTapped { }, 5), { EffectType::request_setup_reboot });
    assert(model.snapshot().phase == Phase::requesting_setup && !model.snapshot().online);
    expect(model.dispatch(SetupTapped { }, 6), { });
    expect(model.dispatch(Tick { }, 100'000'000), { });
    expect(model.dispatch(Connected { }, 7), { });
    expect(model.dispatch(GotIp { }, 8), { });
    expect(model.dispatch(SetupRequested { false }, 9), { EffectType::reconnect });
    assert(model.snapshot().phase == Phase::starting_reconnect && model.snapshot().retry_at == 0);
    expect(model.dispatch(ReconnectCompleted { true }, 10), { });
    expect(model.dispatch(GotIp { }, 11), { });
    expect(model.dispatch(Connected { }, 12), { });
    expect(model.dispatch(GotIp { }, 13), { });
    expect(model.dispatch(SetupTapped { }, 14), { });
    expect(model.dispatch(SetupTapped { }, 14), { EffectType::request_setup_reboot });
    expect(model.dispatch(SetupRequested { true }, 15), { EffectType::reboot });
    assert(model.snapshot().phase == Phase::rebooting);
    expect(model.dispatch(SetupRequested { true }, 16), { });
    expect(model.dispatch(Tick { }, 100'000'000), { });

    StateMachine unsaved;
    setup(unsaved);
    expect(unsaved.dispatch(SetupDisplayFailed { }, 2), { EffectType::stop_portal_and_wifi });
    expect(unsaved.dispatch(SetupTapped { }, 3), { });
    expect(unsaved.dispatch(SetupTapped { }, 3), { EffectType::request_setup_reboot });
    expect(unsaved.dispatch(SetupRequested { false }, 4), { });
    expect(unsaved.dispatch(Tick { }, 100'000'000), { });
    assert(unsaved.snapshot().phase == Phase::recovery_failed);
    expect(unsaved.dispatch(SetupTapped { }, 5), { });
    expect(unsaved.dispatch(SetupTapped { }, 5), { EffectType::request_setup_reboot });
}

void test_saved_retry_and_observations() {
    const Timing timing { .test_timeout = 5, .success_visible = 7, .reconnect_max = 4, .reconnect_initial = 2 };
    StateMachine model(timing);
    expect(model.dispatch(Start { true }, 0), { EffectType::start_saved_connection });
    expect(model.dispatch(Connected { }, 1), { }); // stale before startup completion
    expect(model.dispatch(GotIp { }, 1), { });
    expect(model.dispatch(StartupCompleted { true }, 2), { });
    expect(model.dispatch(GotIp { }, 3), { });
    expect(model.dispatch(Disconnected { }, 4), { }); // stale before association
    expect(model.dispatch(Tick { }, 6), { });
    expect(model.dispatch(Tick { }, 7), { });
    assert(model.snapshot().retry_at == 9 && model.snapshot().retry_delay == 4);
    expect(model.dispatch(Tick { }, 9), { EffectType::reconnect });
    expect(model.dispatch(Tick { }, 13), { });
    expect(model.dispatch(Tick { }, 14), { }); // missing completion is bounded
    expect(model.dispatch(ReconnectCompleted { true }, 15), { });
    expect(model.dispatch(Connected { }, 16), { });
    expect(model.dispatch(GotIp { }, 17), { });
    expect(model.dispatch(Tick { }, 18), { EffectType::reconnect });
    expect(model.dispatch(ReconnectCompleted { false }, 19), { });
    assert(model.snapshot().retry_at == 23 && model.snapshot().retry_delay == 4);
    expect(model.dispatch(Tick { }, 23), { EffectType::reconnect });
    expect(model.dispatch(ReconnectCompleted { true }, 24), { });
    expect(model.dispatch(Connected { }, 25), { });
    expect(model.dispatch(GotIp { }, 26), { });
    assert(model.snapshot().retry_delay == 2);
    expect(model.dispatch(Disconnected { }, 27), { });
    expect(model.dispatch(Tick { }, 29), { EffectType::reconnect });
    expect(model.dispatch(ReconnectCompleted { true }, 30), { });
    expect(model.dispatch(Connected { }, 31), { });
    expect(model.dispatch(WifiObservationLost { }, 32), { });
    expect(model.dispatch(GotIp { }, 33), { });
    expect(model.dispatch(Connected { }, 34), { });
    expect(model.dispatch(GotIp { }, 35), { });
    assert(!model.snapshot().online);
    expect(model.dispatch(Tick { }, 36), { EffectType::reconnect });
    expect(model.dispatch(ReconnectCompleted { true }, 37), { });
    expect(model.dispatch(GotIp { }, 38), { });
    expect(model.dispatch(Connected { }, 39), { });
    expect(model.dispatch(GotIp { }, 40), { });
    expect(model.dispatch(WifiObservationLost { }, 41), { });
    assert(!model.snapshot().online);
}
} // namespace

int main() {
    test_boot_selection();
    test_save_and_reboot();
    test_display_failure();
    test_recovery_request();
    test_saved_retry_and_observations();
}
