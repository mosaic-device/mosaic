// The setup portal without HTTP: submit_setup() stands in for the phone posting the
// setup form, and gets the portal's answers from the same parsing and replies.
#include "../device.hpp"
#include "features/provisioning/adapters/captive_portal.hpp"
#include "features/provisioning/portal_replies.hpp"
#include "features/provisioning/settings_validation.hpp"
#include "support/secure_erase.hpp"

#include <atomic>
#include <mutex>

namespace {
std::mutex g_lock;
bool g_open = false;
mosaic::features::provisioning::adapters::portal::Context g_context { };
std::atomic_bool g_pending { false };
}

namespace mosaic::features::provisioning::adapters::portal {
bool start(const Context& context) {
    const std::lock_guard lock(g_lock);
    if (g_open || !context.status || !context.submit || !context.ip)
        return false;
    g_context = context;
    g_pending = false;
    g_open = true;
    return true;
}
bool stop() {
    const std::lock_guard lock(g_lock);
    g_open = false;
    g_pending = false;
    return true;
}
void consumed() {
    g_pending = false;
}
} // namespace mosaic::features::provisioning::adapters::portal

namespace mosaic::preview::device {
namespace provisioning = features::provisioning;

std::string submit_setup(std::string_view form_body) {
    const std::lock_guard lock(g_lock);
    if (!g_open)
        return "No setup network: the device is not showing setup";
    if (!provisioning::accepts_submission(g_context.status->load(), g_pending.load()))
        return "409 Setup already in progress";
    provisioning::form::Settings settings { };
    if (!provisioning::form::parse(form_body, settings)) {
        support::secure_erase(&settings, sizeof(settings));
        return "400 Invalid SSID, password or WSS URL";
    }
    if (g_pending.exchange(true)) {
        support::secure_erase(&settings, sizeof(settings));
        return "409 Setup already in progress";
    }
    const bool queued = g_context.submit(settings, g_context.user);
    support::secure_erase(&settings, sizeof(settings));
    if (!queued) {
        g_pending = false;
        return "503 Busy; retry shortly";
    }
    return "202 Accepted";
}

std::string setup_status() {
    const std::lock_guard lock(g_lock);
    if (!g_open)
        return "No setup network: the device is not showing setup";
    return provisioning::status_reply(g_context.status->load(), g_pending.load()).plain;
}
} // namespace mosaic::preview::device
