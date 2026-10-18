#include "../presentation/presenter.hpp"
#include "../presentation/action_admission.hpp"
#include <cassert>
using namespace mosaic::features::provisioning;
void online(StateMachine& s) {
    (void)s.dispatch(Start { true }, 0);
    (void)s.dispatch(StartupCompleted { true }, 1);
    (void)s.dispatch(Connected { }, 2);
    (void)s.dispatch(GotIp { }, 3);
}
int main() {
    ViewState text_a;
    ViewState text_b;
    text_a.message = "A previous longer message";
    text_a.message = "Same";
    text_b.message = "Same";
    assert(text_a == text_b); // stale array tails/padding are not presentation semantics
    static_assert(sizeof(ViewState) <= 512);

    assert(!present(StartupView::unavailable).recovery_visible);
    StateMachine s;
    assert(present(s.snapshot()) == present(StartupView::starting));
    (void)s.dispatch(Start { true }, 0);
    assert(present(s.snapshot()).message == "Connecting to saved Wi-Fi...");
    (void)s.dispatch(StartupCompleted { true }, 1);
    assert(s.dispatch(SetupTapped { 1 }, 2).count == 0); // hidden initial connection
    (void)s.dispatch(Connected { }, 3);
    (void)s.dispatch(GotIp { }, 4);
    assert(present(s.snapshot()).heading == "Wi-Fi connected");
    (void)s.dispatch(SetupTapped { 2 }, 10);
    assert(s.snapshot().recovery == RecoveryState::confirming);
    auto confirming = present(s.snapshot());
    (void)s.dispatch(Tick { }, 11);
    assert(present(s.snapshot()) == confirming);
    (void)s.dispatch(Disconnected { }, 12);
    assert(s.snapshot().recovery == RecoveryState::confirming);
    assert(present(s.snapshot()).recovery_prompt == confirming.recovery_prompt);
    (void)s.dispatch(Tick { }, 10'000'010); // exact boundary, even Tick first
    auto effects = s.dispatch(SetupTapped { 3 }, 10'000'010);
    assert(effects.count == 1 && effects.effects[0].type == EffectType::request_setup_reboot);
    assert(!present(s.snapshot()).recovery_enabled);
    assert(s.dispatch(SetupTapped { 4 }, 10'000'011).count == 0);
    (void)s.dispatch(ActionRejected { 5 }, 10'000'012);
    assert(!s.snapshot().admission_rejected);
    effects = s.dispatch(SetupRequested { false }, 10'000'013);
    assert(effects.count == 1 && effects.effects[0].type == EffectType::reconnect);
    auto failure = present(s.snapshot());
    assert(failure.recovery_visible && failure.recovery_enabled);
    assert(failure.message.view().find("Setup unavailable") != std::string_view::npos);
    (void)s.dispatch(ReconnectCompleted { true }, 10'000'014);
    assert(present(s.snapshot()) == failure);
    (void)s.dispatch(SetupTapped { 6 }, 10'000'015);
    (void)s.dispatch(SetupTapped { 7 }, 10'000'016);
    (void)s.dispatch(SetupRequested { false }, 10'000'017);
    assert(present(s.snapshot()) == failure); // repeated identical error is state

    StateMachine offline;
    online(offline);
    (void)offline.dispatch(Disconnected { }, 4);
    assert(present(offline.snapshot()).message == "Wi-Fi offline. Retrying; tap to enter setup.");
    (void)offline.dispatch(Tick { }, 1'000'004);
    (void)offline.dispatch(ReconnectCompleted { false }, 1'000'005);
    assert(present(offline.snapshot()).message == "Wi-Fi unavailable. Retrying; tap to enter setup.");
    (void)offline.dispatch(ActionRejected { 1 }, 1'000'006);
    assert(present(offline.snapshot()).recovery_prompt == "Setup busy. Tap to retry.");
    (void)offline.dispatch(SetupTapped { 2 }, 1'000'007);
    assert(!offline.snapshot().admission_rejected);
    StateMachine unavailable;
    (void)unavailable.dispatch(Start { true }, 0);
    (void)unavailable.dispatch(StartupCompleted { false }, 1);
    assert(present(unavailable.snapshot()) == present(StartupView::unavailable));
    (void)unavailable.dispatch(ActionRejected { 1 }, 2);
    assert(!unavailable.snapshot().admission_rejected);

    StateMachine failed_setup;
    (void)failed_setup.dispatch(Start { false }, 0);
    (void)failed_setup.dispatch(StartupCompleted { false }, 1);
    const auto failed_setup_view = present(failed_setup.snapshot(), 17);
    assert(failed_setup_view == present(StartupView::unavailable, 17));
    assert(failed_setup_view.session == 17 && failed_setup_view.screen == ScreenKind::status);

    StateMachine expired;
    online(expired);
    (void)expired.dispatch(SetupTapped { 1 }, 20);
    assert(expired.dispatch(SetupTapped { 2 }, 10'000'021).count == 0);
    assert(expired.snapshot().confirmation_deadline == 20'000'021);
    (void)expired.dispatch(Tick { }, 20'000'022);
    assert(expired.snapshot().recovery == RecoveryState::idle);

    StateMachine race;
    online(race);
    ActionAdmission admission;
    const auto first = admission.issue();
    const auto rejected = admission.issue();
    admission.rejected(rejected);
    admission.rejected(admission.issue());
    (void)race.dispatch(ActionRejected { admission.take_rejected().id }, 4);
    assert(admission.take_rejected().id == 0);
    assert(race.snapshot().admission_rejected);
    (void)race.dispatch(SetupTapped { first }, 5); // older accepted action is not dropped
    assert(race.snapshot().recovery == RecoveryState::confirming);
    const auto newer = admission.issue();
    (void)race.dispatch(SetupTapped { newer }, 6);
    (void)race.dispatch(ActionRejected { rejected }, 7); // late rejection cannot replace accepted recovery
    assert(!race.snapshot().admission_rejected);
    assert(race.snapshot().phase == Phase::requesting_setup);

    StateMachine context;
    online(context);
    const auto old_context = context.snapshot().action_context;
    (void)context.dispatch(SetupTapped { 1, old_context }, 4);
    (void)context.dispatch(SetupTapped { 2, old_context }, 5);
    (void)context.dispatch(SetupRequested { false }, 6);
    (void)context.dispatch(SetupTapped { 3, old_context }, 7);
    (void)context.dispatch(ActionRejected { 4, old_context }, 8);
    assert(context.snapshot().recovery == RecoveryState::failed);
    assert(!context.snapshot().admission_rejected);
    (void)context.dispatch(ReconnectCompleted { true }, 9);
    const auto current_context = context.snapshot().action_context;
    (void)context.dispatch(SetupTapped { 5, current_context }, 10);
    (void)context.dispatch(Tick { }, 10'000'011);
    assert(context.dispatch(SetupTapped { 6, current_context }, 10'000'012).count == 0);
    assert(context.snapshot().recovery == RecoveryState::confirming);

    StateMachine setup;
    (void)setup.dispatch(Start { false }, 0);
    assert(present(setup.snapshot(), 1).screen == ScreenKind::setup);
    (void)setup.dispatch(StartupCompleted { true }, 1);
    form::Settings settings { };
    (void)setup.dispatch(Submit { settings }, 2);
    assert(present(setup.snapshot(), 1).screen == ScreenKind::setup);
    (void)setup.dispatch(Saved { false }, 3);
    assert(!present(setup.snapshot(), 1).setup_failure.empty());
    (void)setup.dispatch(Submit { settings }, 4);
    (void)setup.dispatch(Saved { true }, 5);
    assert(present(setup.snapshot(), 1).message.view().find("Settings saved") == 0);
    auto saved = present(setup.snapshot(), 1);
    (void)setup.dispatch(Tick { }, 6);
    assert(present(setup.snapshot(), 1) == saved);
    (void)setup.dispatch(Tick { }, 6'000'005);
    assert(present(setup.snapshot(), 1).message == "Restarting...");
}
