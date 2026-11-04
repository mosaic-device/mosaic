#include "../portal_replies.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>

using namespace mosaic::features::provisioning;

namespace {
bool says(const StatusReply& reply, const char* state) {
    char expected[48];
    std::snprintf(expected, sizeof(expected), "{\"state\":\"%s\"}", state);
    return std::strcmp(reply.structured, expected) == 0 && reply.plain[0];
}
}

int main() {
    assert(says(status_reply(PortalStatus::idle, false), "ready"));
    assert(says(status_reply(PortalStatus::ready, false), "ready"));
    assert(says(status_reply(PortalStatus::saving, false), "saving"));
    assert(says(status_reply(PortalStatus::saved, false), "saved"));
    assert(says(status_reply(PortalStatus::storage_error, false), "storage_error"));
    // A handed-off submission reads as saving before the owner publishes it.
    for (const auto status : { PortalStatus::idle, PortalStatus::ready, PortalStatus::storage_error })
        assert(says(status_reply(status, true), "saving"));
    // The saved message tells the owner pairing is still needed.
    assert(std::strstr(status_reply(PortalStatus::saved, false).plain, "pairing"));

    // Ready and failed portals take a submission; a pending, saving or saved one does not.
    assert(accepts_submission(PortalStatus::ready, false));
    assert(accepts_submission(PortalStatus::storage_error, false));
    assert(!accepts_submission(PortalStatus::ready, true));
    assert(!accepts_submission(PortalStatus::saving, false));
    assert(!accepts_submission(PortalStatus::saved, false));

    // Saves are screened in order: admission, size, type, then the throttle.
    const auto code = [](const std::optional<HttpReply>& reply) { return reply ? reply->code : 0; };
    AttemptThrottle throttle;
    constexpr int64_t kStart = 50'000'000;
    assert(code(screen_save(PortalStatus::saving, false, 10, kFormType, throttle, kStart)) == 409);
    assert(code(screen_save(PortalStatus::ready, true, 10, kFormType, throttle, kStart)) == 409);
    assert(code(screen_save(PortalStatus::ready, false, 0, kFormType, throttle, kStart)) == 400);
    assert(code(screen_save(PortalStatus::ready, false, -1, kFormType, throttle, kStart)) == 400);
    assert(code(screen_save(PortalStatus::ready, false, kFormBodyLimit + 1, kFormType, throttle, kStart)) == 400);
    assert(code(screen_save(PortalStatus::ready, false, 10, "text/plain", throttle, kStart)) == 400);
    assert(code(screen_save(PortalStatus::ready, false, 10, "application/x-www-form-urlencoded; x", throttle, kStart))
        == 400);
    // Rejected requests never used the interval, so the first good one is admitted.
    assert(!screen_save(PortalStatus::ready, false, kFormBodyLimit, kFormType, throttle, kStart));
    assert(code(screen_save(PortalStatus::storage_error, false, 10, kFormType, throttle, kStart + 1)) == 429);
    assert(
        code(screen_save(PortalStatus::ready, false, 10, kFormType, throttle, kStart + kAttemptIntervalUs - 1)) == 429);
    assert(!screen_save(PortalStatus::ready, false, 10, kFormType, throttle, kStart + kAttemptIntervalUs));
    // The first attempt after boot is never throttled, even at time zero.
    AttemptThrottle fresh;
    assert(fresh.admit(0));

    // Status lines for every reply the portal sends.
    for (const auto& reply : { kInvalidSettings, kInProgress, kBusy, kAccepted, HttpReply { 429, "" } })
        assert(std::strncmp(status_line(reply.code), std::to_string(reply.code).c_str(), 3) == 0);
    assert(kAccepted.html && !kBusy.html);
}
