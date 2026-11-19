#include "presenter.hpp"
#include <array>
#include <cstdio>
namespace mosaic::features::provisioning {
ViewState present(StartupView startup, uint32_t session) {
    ViewState view;
    view.session = session;
    view.heading = "Mosaic";
    view.starting = startup == StartupView::starting;
    view.recovery_prompt = kEnterSetup;
    view.message = startup == StartupView::starting
        ? "Starting Wi-Fi setup..."
        : "Setup unavailable. Restart the device or connect a service technician.";
    if (startup == StartupView::unavailable)
        view.heading = "Offline";
    return view;
}
std::string_view server_host(std::string_view url) {
    constexpr std::string_view kScheme = "wss://";
    if (!url.starts_with(kScheme))
        return { };
    url.remove_prefix(kScheme.size());
    return url.substr(0, url.find_first_of("/?#"));
}

namespace {
    // The pairing screen: the code large, where to enter it, and how long it lasts. A code
    // that runs out is replaced by the server once it can reach the device; until then the
    // screen says the old one no longer works.
    void show_pairing(ViewState& v, const Pairing& pairing) {
        v.heading = "Pair this Mosaic";
        const std::string_view host = server_host(pairing.server_url);
        const events::PairingCode& code = pairing.code;
        if (code.empty()) {
            v.message = "Getting a pairing code...";
            return;
        }
        if (pairing.now_ms >= code.expires_at_ms) {
            v.message = "That code expired. Waiting for a new one...";
            return;
        }
        std::array<char, 10> shown { };
        std::string_view text = code.view();
        std::snprintf(shown.data(), shown.size(), "%.4s-%.4s", text.data(), text.data() + 4);
        v.code = shown.data();
        // Whole seconds left, rounded up, so it never shows 0:00 while the code still works.
        const uint64_t left_s = (code.expires_at_ms - pairing.now_ms + 999) / 1'000;
        std::array<char, 192> message { };
        std::snprintf(message.data(), message.size(), "Enter it in Mosaic at %.*s.\nExpires in %u:%02u.",
            static_cast<int>(host.size()), host.data(), static_cast<unsigned>(left_s / 60),
            static_cast<unsigned>(left_s % 60));
        v.message = message.data();
    }
} // namespace

ViewState present(const Snapshot& s, uint32_t session, events::Status server, const Pairing& pairing) {
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
        v.setup = true;
        if (s.portal_status == PortalStatus::storage_error)
            v.setup_failure = "Saving settings failed. Please try again.";
        return v;
    case Phase::saved_visible:
        v.message = "Settings saved. Restarting to connect to Wi-Fi...";
        v.setup = true;
        return v;
    case Phase::requesting_setup:
        v.setup = true;
        v.heading = "Offline";
        v.message = "Opening setup...";
        v.recovery_visible = true;
        v.recovery_enabled = false;
        v.recovery_prompt = "Opening setup...";
        return v;
    case Phase::rebooting:
        v.message = "Restarting...";
        v.setup = true;
        return v;
    case Phase::online:
        v.heading = "Wi-Fi connected";
        v.online = true;
        v.recovery_visible = true;
        switch (server) {
        case events::Status::disconnected:
        case events::Status::reconnecting:
            v.message = "Connecting to the server...";
            break;
        case events::Status::pairing:
            show_pairing(v, pairing);
            break;
        case events::Status::connected:
            // Shown for a moment after pairing, before Home.
            v.heading = "Paired";
            v.message = "Opening Home...";
            v.recovery_visible = false;
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
