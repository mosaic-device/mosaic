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

} // namespace mosaic::features::provisioning
