#pragma once
#include "../state_machine.hpp"
#include "../../../events/pairing_code.hpp"
#include "../../../events/status.hpp"
#include <cstdint>
#include <string_view>
#include "view_state.hpp"
namespace mosaic::features::provisioning {
// The recovery prompt when nothing is in progress.
inline constexpr std::string_view kEnterSetup = "Enter setup mode";
// What the pairing screen needs: the code the server issued, the time now on the code's
// clock, and the saved server URL, whose host tells the owner where to enter the code.
struct Pairing {
    events::PairingCode code;
    uint64_t now_ms = 0;
    std::string_view server_url;
};

// The host of a wss:// URL, with its port if it has one: what the owner opens in a browser.
// Empty if `url` isn't wss://.
std::string_view server_host(std::string_view url);

// `server` is the connection's events::Status. It decides what the online screen says, where
// the owner learns whether the saved server can be reached and finds the pairing code.
ViewState present(const Snapshot& state, uint32_t session = 0, events::Status server = events::Status::disconnected,
    const Pairing& pairing = { });
ViewState present(StartupView startup, uint32_t session = 0);
}
