#pragma once
#include "../state_machine.hpp"
#include "../../../events/status.hpp"
#include "view_state.hpp"
namespace mosaic::features::provisioning {
// `server` is the connection's events::Status. It decides what the online screen says, where
// the owner learns whether the saved server can be reached.
ViewState present(const Snapshot& state, uint32_t session = 0, events::Status server = events::Status::disconnected);
ViewState present(StartupView startup, uint32_t session = 0);
}
