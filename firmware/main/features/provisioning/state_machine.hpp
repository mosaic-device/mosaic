#pragma once

#include "settings_validation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace mosaic::features::provisioning {

// All times are monotonic microseconds. Dispatch one event at a time, including Tick
// after each queue wait; execute effects in order and dispatch their completion events.
struct Timing {
    // Bounds saved STA startup and reconnect attempts, not setup candidates.
    int64_t test_timeout = 20'000'000;
    int64_t success_visible = 6'000'000;
    int64_t reconnect_max = 60'000'000;
    int64_t reconnect_initial = 1'000'000;
};

enum class Phase : uint8_t {
    unstarted,
    starting_saved,
    starting_reconnect,
    opening_portal,
    saved_sta,
    portal,
    saving,
    saved_visible,
    requesting_setup,
    rebooting,
    offline,
    online,
    startup_failed,
    recovery_failed
};
enum class PortalStatus : uint8_t { idle, ready, saving, saved, storage_error };

struct Start {
    bool has_saved;
    bool setup_requested = false;
};
struct StartupCompleted {
    bool ok;
}; // StartSavedConnection or OpenPortal
enum class RecoveryState : uint8_t { idle, confirming, requesting, failed };
enum class VisibleReason : uint8_t { connecting, retrying, unavailable, recovery_error };
struct SetupTapped {
    uint64_t admission_id = 0;
    uint32_t context = 0;
};
struct ActionRejected {
    uint64_t admission_id;
    uint32_t context = 0;
};
struct SetupRequested {
    bool ok;
}; // RequestSetupReboot
struct Submit {
    form::Settings settings;
}; // caller validates before dispatch
struct Connected { };
struct GotIp { };
struct Disconnected { };
// Owner detected lost Wi-Fi observations (e.g. its event queue overflowed).
// Invalidates a saved STA association; candidate persistence is independent.
struct WifiObservationLost { };
// The owner could not restore setup credentials while the AP was active.
struct SetupDisplayFailed { };
struct Saved {
    bool ok;
}; // PersistCandidate
struct ReconnectCompleted {
    bool ok;
}; // Reconnect initiation; fresh Connected/GotIp report actual connection success
struct Tick { };
using Event = std::variant<Start, StartupCompleted, SetupTapped, ActionRejected, SetupRequested, Submit, Connected,
    GotIp, Disconnected, WifiObservationLost, SetupDisplayFailed, Saved, ReconnectCompleted, Tick>;

enum class EffectType : uint8_t {
    start_saved_connection,
    open_portal,
    request_setup_reboot,
    reboot,
    persist_candidate,
    reconnect,
    stop_portal_and_wifi
};
struct Effect {
    EffectType type;
    // Populated only for persist_candidate. Treat returned effects
    // as sensitive and clear them in the adapter after use.
    form::Settings settings { };
};
struct Result {
    std::array<Effect, 3> effects { };
    size_t count = 0;
};
struct Snapshot {
    Phase phase;
    PortalStatus portal_status;
    bool has_saved;
    bool online;
    bool portal_active;
    int64_t retry_at;
    int64_t retry_delay;
    RecoveryState recovery;
    int64_t confirmation_deadline;
    VisibleReason reason;
    bool admission_rejected;
    uint32_t action_context;
};

class StateMachine {
public:
    explicit StateMachine(Timing timing = { });
    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;
    ~StateMachine();

    [[nodiscard]] Result dispatch(const Event& event, int64_t now);
    [[nodiscard]] Snapshot snapshot() const;

private:
    void clear_candidate();
    void schedule_retry(int64_t now);
    void begin_saved_retry(Result& result, int64_t now);
    bool recovery_eligible() const;
    RecoveryState recovery_ = RecoveryState::idle;
    int64_t confirmation_deadline_ = 0;
    VisibleReason reason_ = VisibleReason::connecting;
    bool admission_rejected_ = false;
    uint64_t latest_admission_ = 0;
    uint32_t action_context_ = 1;
    Timing timing_;
    Phase phase_ = Phase::unstarted;
    PortalStatus portal_status_ = PortalStatus::idle;
    bool has_saved_ = false;
    bool online_ = false;
    bool portal_active_ = false;
    bool associated_ = false;
    bool observations_lost_ = false;
    form::Settings candidate_ { };
    int64_t deadline_ = 0;
    int64_t reboot_at_ = 0;
    int64_t retry_at_ = 0;
    int64_t retry_delay_;
};

} // namespace mosaic::features::provisioning
