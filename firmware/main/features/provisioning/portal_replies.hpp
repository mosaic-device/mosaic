#pragma once

#include "settings_validation.hpp"
#include "state_machine.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mosaic::features::provisioning {

// What the setup page shows for the portal's state, as text and as JSON.
struct StatusReply {
    const char* plain;
    const char* structured;
};
// pending means a submission was handed off before the owner published its saving
// state; that is transport admission, not a second authoritative feature state.
StatusReply status_reply(PortalStatus status, bool pending);

// Whether the portal takes a new submission: not while one is pending, saving, or saved.
bool accepts_submission(PortalStatus status, bool pending);

// An HTTP answer to the setup page. 400s go out as the server's error page; the rest
// as a status line and this text.
struct HttpReply {
    int code;
    const char* text;
    bool html = false;
};
// "409 Conflict" and so on, for the codes these replies use.
const char* status_line(int code);

inline constexpr size_t kFormBodyLimit = form::kEncodedBodyLimit;
inline constexpr std::string_view kFormType = "application/x-www-form-urlencoded";
inline constexpr int64_t kAttemptIntervalUs = 3'000'000;

// One form submission per interval across every connection, so guessing passwords
// from the setup network is slow. Any task.
class AttemptThrottle {
public:
    bool admit(int64_t now_us);

private:
    std::atomic<int64_t> last_ { 0 };
};

// Checks a save request before its body is read, in order: admission, size, type, then
// throttling, so only a well-formed request uses up the interval. A reply rejects the
// request; none means read the body and parse it.
std::optional<HttpReply> screen_save(PortalStatus status, bool pending, int64_t content_length,
    std::string_view content_type, AttemptThrottle& throttle, int64_t now_us);

// The replies once the body is read.
inline constexpr HttpReply kInvalidSettings { 400, "Invalid SSID, password or WSS URL" };
inline constexpr HttpReply kInProgress { 409, "Setup already in progress" };
inline constexpr HttpReply kBusy { 503, "Busy; retry shortly" };
inline constexpr HttpReply kAccepted { 202,
    "<p>Saving settings without testing Wi-Fi. <a href='/status'>Check status</a> or watch the device "
    "display.</p>",
    true };

} // namespace mosaic::features::provisioning
