#pragma once

#include "state_machine.hpp"

#include <cstddef>
#include <cstdint>

namespace mosaic::features::provisioning {

// Which radio events the owner still trusts. Each Wi-Fi event carries a sequence number;
// starting a connection, or resyncing after events were lost, makes every earlier one
// stale. Owner task only.
class WifiObservation {
public:
    // Ignores every event numbered up to latest, the newest one issued so far.
    void ignore_through(uint32_t latest) {
        ignored_through_ = latest;
    }
    // Counts across wraparound, so the newest events stay fresh after 2^32 of them.
    bool stale(uint32_t sequence) const {
        return static_cast<int32_t>(sequence - ignored_through_) <= 0;
    }

private:
    uint32_t ignored_through_ = 0;
};

// While joining the saved network, only that network's association counts; another AP
// cannot complete it. Other phases accept any association.
bool needs_saved_network(Phase phase);
// Phases that follow the station, so lost events must be resynced.
bool follows_station(Phase phase);

// What the radio reports when the owner asks directly.
struct Radio {
    bool connected;
    bool has_ip;
};

// The events that bring the state machine up to date with radio after events were
// lost, in dispatch order; none in phases that do not follow the station.
struct Resync {
    Event events[2];
    size_t count = 0;
};
Resync resync(Phase phase, Radio radio);

} // namespace mosaic::features::provisioning
