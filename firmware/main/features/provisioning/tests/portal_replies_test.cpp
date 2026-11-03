#include "../portal_replies.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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
}
