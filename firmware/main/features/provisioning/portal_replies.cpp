#include "portal_replies.hpp"

namespace mosaic::features::provisioning {

StatusReply status_reply(PortalStatus status, bool pending) {
    switch (pending ? PortalStatus::saving : status) {
    case PortalStatus::saving:
        return { "Saving settings. Refresh in a moment.", "{\"state\":\"saving\"}" };
    case PortalStatus::saved:
        return { "Wi-Fi saved. Mosaic will reboot in about six seconds, then try connecting. Device pairing is "
                 "still required.",
            "{\"state\":\"saved\"}" };
    case PortalStatus::storage_error:
        return { "Saving failed. Settings were not confirmed saved. Retry setup.", "{\"state\":\"storage_error\"}" };
    default:
        return { "Ready for setup.", "{\"state\":\"ready\"}" };
    }
}

bool accepts_submission(PortalStatus status, bool pending) {
    return !pending && status != PortalStatus::saving && status != PortalStatus::saved;
}

const char* status_line(int code) {
    switch (code) {
    case 202:
        return "202 Accepted";
    case 400:
        return "400 Bad Request";
    case 409:
        return "409 Conflict";
    case 429:
        return "429 Too Many Requests";
    case 503:
        return "503 Service Unavailable";
    default:
        return "500 Internal Server Error";
    }
}

bool AttemptThrottle::admit(int64_t now_us) {
    int64_t previous = last_.load();
    // Losing the exchange means another request took this interval.
    return (previous == 0 || now_us - previous >= kAttemptIntervalUs)
        && last_.compare_exchange_strong(previous, now_us);
}

std::optional<HttpReply> screen_save(PortalStatus status, bool pending, int64_t content_length,
    std::string_view content_type, AttemptThrottle& throttle, int64_t now_us) {
    if (!accepts_submission(status, pending))
        return kInProgress;
    if (content_length <= 0 || content_length > static_cast<int64_t>(kFormBodyLimit))
        return HttpReply { 400, "Invalid form size" };
    if (content_type != kFormType)
        return HttpReply { 400, "Expected form data" };
    if (!throttle.admit(now_us))
        return HttpReply { 429, "Wait a few seconds before retrying" };
    return std::nullopt;
}

} // namespace mosaic::features::provisioning
