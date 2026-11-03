#pragma once

#include "state_machine.hpp"

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

} // namespace mosaic::features::provisioning
