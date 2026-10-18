#pragma once
#include "../state_machine.hpp"
#include "view_state.hpp"
namespace mosaic::features::provisioning {
ViewState present(const Snapshot& state, uint32_t session = 0);
ViewState present(StartupView startup, uint32_t session = 0);
}
