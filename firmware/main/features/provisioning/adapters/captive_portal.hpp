#pragma once

#include <atomic>

#include "../state_machine.hpp"
#include "esp_err.h"
#include "esp_netif.h"

namespace mosaic::features::provisioning::adapters::portal {
using PortalStatus = mosaic::features::provisioning::PortalStatus;
using Settings = form::Settings;
// Called on the HTTP task. Must only attempt a nonblocking, bounded handoff.
using Submit = bool (*)(const Settings&, void*);

struct Context {
    esp_netif_t* ap_netif;

    std::atomic<PortalStatus>* status;
    Submit submit;
    void* user;
    const char* ip;
};

// All lifecycle calls run on the network owner task, after the AP display gate.
bool start(const Context& context);
// False means shutdown is incomplete. Retain all Context dependencies and retry
// stop(), or fail-safe restart before destroying them. Never force-delete DNS.
// A late DNS completion is reaped by the next stop()/start(); pending DNS owns
// only adapter-static state and its copied IPv4 address, not Context pointers.
[[nodiscard]] bool stop();
// Releases the one outstanding HTTP submission after owner receipt (or on stop).
void consumed();
} // namespace mosaic::features::provisioning::adapters::portal
