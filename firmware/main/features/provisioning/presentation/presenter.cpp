#include "presenter.hpp"
namespace mosaic::features::provisioning {
ViewState present(StartupView startup, uint32_t session) {
    ViewState view;
    view.session = session;
    view.heading = "Mosaic";
    view.recovery_prompt = "Enter setup mode";
    view.message = startup == StartupView::starting
        ? "Starting Wi-Fi setup..."
        : "Setup unavailable. Restart the device or connect a service technician.";
    if (startup == StartupView::unavailable)
        view.heading = "Offline";
    return view;
}
ViewState present(const Snapshot& s, uint32_t session) {
    ViewState v = present(StartupView::starting);
    v.session = session;
    switch (s.phase) {
    case Phase::unstarted:
        return present(StartupView::starting);
    case Phase::startup_failed:
        return present(StartupView::unavailable, session);
    case Phase::opening_portal:
    case Phase::portal:
    case Phase::saving:
        v.screen = ScreenKind::setup;
        if (s.portal_status == PortalStatus::storage_error)
            v.setup_failure = "Saving settings failed. Please try again.";
        return v;
    case Phase::saved_visible:
        v.message = "Settings saved. Restarting to connect to Wi-Fi...";
        return v;
    case Phase::requesting_setup:
        v.heading = "Offline";
        v.message = "Opening setup...";
        v.recovery_visible = true;
        v.recovery_enabled = false;
        v.recovery_prompt = "Opening setup...";
        return v;
    case Phase::rebooting:
        v.message = "Restarting...";
        return v;
    case Phase::online:
        v.heading = "Wi-Fi connected";
        v.message = "Waiting for device pairing.";
        v.recovery_visible = true;
        break;
    case Phase::starting_saved:
    case Phase::saved_sta:
    case Phase::starting_reconnect:
    case Phase::offline:
    case Phase::recovery_failed:
        if (s.reason == VisibleReason::connecting && s.phase != Phase::offline && s.phase != Phase::recovery_failed) {
            v.message = "Connecting to saved Wi-Fi...";
        } else {
            v.heading = "Offline";
            v.recovery_visible = true;
            v.message = s.reason == VisibleReason::unavailable ? "Wi-Fi unavailable. Retrying; tap to enter setup."
                                                               : "Wi-Fi offline. Retrying; tap to enter setup.";
        }
        break;
    }
    if (s.recovery == RecoveryState::failed) {
        v.heading = "Offline";
        v.message = "Setup unavailable. Restart the device or tap to retry setup.";
        v.recovery_visible = true;
    }
    if (s.recovery == RecoveryState::confirming)
        v.recovery_prompt = "Tap again to confirm setup";
    if (s.admission_rejected)
        v.recovery_prompt = "Setup busy. Tap to retry.";
    return v;
}
}
