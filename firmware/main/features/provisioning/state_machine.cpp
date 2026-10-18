#include "state_machine.hpp"
#include "../../support/secure_erase.hpp"

#include <algorithm>
#include <cassert>
#include <type_traits>

namespace mosaic::features::provisioning {

StateMachine::StateMachine(Timing timing)
    : timing_(timing)
    , retry_delay_(timing.reconnect_initial) { }
StateMachine::~StateMachine() {
    clear_candidate();
}

void StateMachine::clear_candidate() {
    support::secure_erase(candidate_);
}

void StateMachine::schedule_retry(int64_t now) {
    retry_at_ = now + retry_delay_;
    retry_delay_ = std::min(retry_delay_ * 2, timing_.reconnect_max);
}

void StateMachine::begin_saved_retry(Result& result, int64_t now) {
    phase_ = Phase::starting_reconnect;
    associated_ = false;
    // The owner invalidates queued observations before executing reconnect.
    observations_lost_ = false;
    retry_at_ = 0;
    deadline_ = now + timing_.test_timeout;
    assert(result.count < result.effects.size());
    result.effects[result.count++].type = EffectType::reconnect;
}

Snapshot StateMachine::snapshot() const {
    return { phase_, portal_status_, has_saved_, online_, portal_active_, retry_at_, retry_delay_, recovery_,
        confirmation_deadline_, reason_, admission_rejected_, action_context_ };
}

bool StateMachine::recovery_eligible() const {
    return !portal_active_
        && (phase_ == Phase::online || phase_ == Phase::offline || phase_ == Phase::recovery_failed
            || recovery_ == RecoveryState::failed || recovery_ == RecoveryState::confirming
            || ((phase_ == Phase::saved_sta || phase_ == Phase::starting_reconnect)
                && reason_ != VisibleReason::connecting));
}

Result StateMachine::dispatch(const Event& event, int64_t now) {
    const bool was_eligible = recovery_eligible();
    const bool was_online = online_;
    Result result;
    auto emit = [&](EffectType type) {
        assert(result.count < result.effects.size());
        result.effects[result.count++].type = type;
    };
    auto emit_settings = [&](EffectType type) {
        assert(result.count < result.effects.size());
        result.effects[result.count++] = { type, candidate_ };
    };
    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, Start>) {
                if (phase_ != Phase::unstarted)
                    return;
                has_saved_ = e.has_saved;
                if (has_saved_ && !e.setup_requested) {
                    phase_ = Phase::starting_saved;
                    deadline_ = now + timing_.test_timeout;
                    emit(EffectType::start_saved_connection);
                } else {
                    phase_ = Phase::opening_portal;
                    emit(EffectType::open_portal);
                }
            } else if constexpr (std::is_same_v<T, StartupCompleted>) {
                if (phase_ != Phase::starting_saved && phase_ != Phase::opening_portal)
                    return;
                if (!e.ok) {
                    phase_ = Phase::startup_failed;
                    return; // owner reports startup failure and performs cleanup
                }
                if (phase_ == Phase::opening_portal) {
                    phase_ = Phase::portal;
                    portal_active_ = true;
                    portal_status_ = PortalStatus::ready;
                } else {
                    phase_ = Phase::saved_sta;
                    associated_ = false;
                    deadline_ = now + timing_.test_timeout;
                }
            } else if constexpr (std::is_same_v<T, SetupTapped>) {
                if (e.context && e.context != action_context_)
                    return;
                if (portal_active_ || phase_ == Phase::unstarted || phase_ == Phase::startup_failed
                    || phase_ == Phase::opening_portal || phase_ == Phase::requesting_setup
                    || phase_ == Phase::rebooting)
                    return;
                if (!recovery_eligible())
                    return;
                if (e.admission_id >= latest_admission_) {
                    latest_admission_ = e.admission_id;
                    admission_rejected_ = false;
                }
                if (recovery_ != RecoveryState::confirming || now > confirmation_deadline_) {
                    recovery_ = RecoveryState::confirming;
                    confirmation_deadline_ = now + 10'000'000;
                    return;
                }
                recovery_ = RecoveryState::requesting;
                confirmation_deadline_ = 0;
                online_ = false;
                associated_ = false;
                retry_at_ = 0;
                phase_ = Phase::requesting_setup;
                emit(EffectType::request_setup_reboot);
            } else if constexpr (std::is_same_v<T, ActionRejected>) {
                if (e.context && e.context != action_context_)
                    return;
                if (e.admission_id > latest_admission_ && recovery_eligible()
                    && recovery_ != RecoveryState::requesting) {
                    latest_admission_ = e.admission_id;
                    admission_rejected_ = true;
                }
            } else if constexpr (std::is_same_v<T, SetupRequested>) {
                if (phase_ != Phase::requesting_setup)
                    return;
                if (e.ok) {
                    phase_ = Phase::rebooting;
                    emit(EffectType::reboot);
                } else {
                    recovery_ = RecoveryState::failed;
                    reason_ = VisibleReason::recovery_error;
                    phase_ = Phase::recovery_failed;
                    if (has_saved_)
                        begin_saved_retry(result, now);
                }
            } else if constexpr (std::is_same_v<T, Submit>) {
                if (phase_ != Phase::portal || portal_status_ == PortalStatus::saved)
                    return;
                clear_candidate();
                candidate_ = e.settings;
                phase_ = Phase::saving;
                portal_status_ = PortalStatus::saving;
                emit_settings(EffectType::persist_candidate);
            } else if constexpr (std::is_same_v<T, Connected>) {
                if (phase_ == Phase::saved_sta)
                    associated_ = true;
            } else if constexpr (std::is_same_v<T, GotIp>) {
                if (observations_lost_)
                    return; // don't infer a connection from a late IP event after overflow
                if (!portal_active_ && phase_ == Phase::saved_sta && associated_) {
                    online_ = true;
                    retry_delay_ = timing_.reconnect_initial;
                    retry_at_ = 0;
                    phase_ = Phase::online;
                    if (recovery_ != RecoveryState::confirming)
                        recovery_ = RecoveryState::idle;
                    reason_ = VisibleReason::connecting;
                    admission_rejected_ = false;
                }
            } else if constexpr (std::is_same_v<T, Saved>) {
                if (phase_ != Phase::saving)
                    return;
                if (e.ok) {
                    has_saved_ = true;
                    online_ = false;
                    portal_status_ = PortalStatus::saved;
                    phase_ = Phase::saved_visible;
                    reboot_at_ = now + timing_.success_visible;
                } else {
                    portal_status_ = PortalStatus::storage_error;
                    phase_ = Phase::portal;
                }
                clear_candidate();
            } else if constexpr (std::is_same_v<T, SetupDisplayFailed>) {
                if (!portal_active_ || phase_ == Phase::rebooting)
                    return;
                portal_active_ = false;
                portal_status_ = PortalStatus::idle;
                recovery_ = RecoveryState::failed;
                confirmation_deadline_ = 0;
                reason_ = VisibleReason::recovery_error;
                phase_ = Phase::recovery_failed;
                online_ = false;
                clear_candidate();
                emit(EffectType::stop_portal_and_wifi);
            } else if constexpr (std::is_same_v<T, WifiObservationLost>) {
                if (phase_ != Phase::saved_sta && phase_ != Phase::online)
                    return;
                observations_lost_ = true;
                associated_ = false;
                online_ = false;
                phase_ = Phase::offline;
                if (recovery_ != RecoveryState::failed)
                    reason_ = VisibleReason::retrying;
                schedule_retry(now);
            } else if constexpr (std::is_same_v<T, Disconnected>) {
                if (!portal_active_ && has_saved_
                    && (phase_ == Phase::online || (phase_ == Phase::saved_sta && associated_))) {
                    online_ = false;
                    associated_ = false;
                    phase_ = Phase::offline;
                    if (recovery_ != RecoveryState::failed)
                        reason_ = VisibleReason::retrying;
                    schedule_retry(now);
                }
            } else if constexpr (std::is_same_v<T, ReconnectCompleted>) {
                if (phase_ != Phase::starting_reconnect)
                    return;
                associated_ = false;
                if (e.ok) {
                    phase_ = Phase::saved_sta;
                    deadline_ = now + timing_.test_timeout;
                } else {
                    phase_ = Phase::offline;
                    if (recovery_ != RecoveryState::failed)
                        reason_ = VisibleReason::unavailable;
                    schedule_retry(now);
                }
            } else if constexpr (std::is_same_v<T, Tick>) {
                // Exact deadline remains confirmable: Tick expires only strictly after it.
                if (recovery_ == RecoveryState::confirming && now > confirmation_deadline_) {
                    recovery_ = RecoveryState::idle;
                    confirmation_deadline_ = 0;
                }
                if (phase_ == Phase::saved_visible && now >= reboot_at_) {
                    phase_ = Phase::rebooting;
                    emit(EffectType::reboot);
                }
                if ((phase_ == Phase::saved_sta || phase_ == Phase::starting_reconnect) && now >= deadline_) {
                    phase_ = Phase::offline;
                    associated_ = false;
                    if (recovery_ != RecoveryState::failed)
                        reason_ = VisibleReason::retrying;
                    schedule_retry(now);
                } else if (phase_ == Phase::offline && has_saved_ && now >= retry_at_) {
                    begin_saved_retry(result, now);
                }
            }
        },
        event);
    // Invalidate queued taps/rejections across hidden/pending/session contexts.
    // Wi-Fi updates during confirmation retain the same action context.
    if (was_eligible != recovery_eligible() || (was_online != online_ && recovery_ != RecoveryState::confirming)) {
        if (!++action_context_)
            ++action_context_;
        admission_rejected_ = false;
    }
    return result;
}

} // namespace mosaic::features::provisioning
