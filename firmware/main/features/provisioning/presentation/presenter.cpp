#include "presenter.hpp"
namespace mosaic::features::provisioning {
ViewState present(StartupView startup, uint32_t session) {
    ViewState view;
    view.session = session;
    view.heading = "Mosaic";
    view.starting = startup == StartupView::starting;
    view.recovery_prompt = "Enter setup mode";
    view.message = startup == StartupView::starting
        ? "Starting Wi-Fi setup..."
        : "Setup unavailable. Restart the device or connect a service technician.";
    if (startup == StartupView::unavailable)
        view.heading = "Offline";
    return view;
}
ViewState present(const Snapshot& s, uint32_t session, events::Status server) {
    ViewState v = present(StartupView::starting);
    v.session = session;
    v.starting = false;
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
        v.online = true;
        v.recovery_visible = true;
        switch (server) {
        case events::Status::disconnected:
            v.message = "Connecting to the server...";
            break;
        case events::Status::pairing:
        case events::Status::connected:
            v.message = "Waiting for device pairing.";
            break;
        case events::Status::error:
            // Setup recovery is how the owner corrects a wrong address.
            v.heading = "Can't reach the server";
            v.message = "Retrying. You can change the address in setup.";
            break;
        case events::Status::update_required:
            // A new address won't help, so there is nothing to press.
            v.heading = "Update required";
            v.message = "The server needs newer firmware on this device.";
            v.recovery_visible = false;
            break;
        }
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
            v.message = s.reason == VisibleReason::unavailable ? "Your Wi-Fi network isn't available. Retrying."
                                                               : "Can't reach your Wi-Fi. Retrying.";
        }
        break;
    }
    if (s.recovery == RecoveryState::failed) {
        v.heading = "Offline";
        v.message = "Setup unavailable. Try again, or restart the device.";
        v.recovery_visible = true;
    }
    if (s.recovery == RecoveryState::confirming)
        v.recovery_prompt = "Press again to confirm";
    if (s.admission_rejected)
        v.recovery_prompt = "Setup busy. Try again.";
    return v;
}
}
