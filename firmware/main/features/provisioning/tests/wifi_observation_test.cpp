#include "../wifi_observation.hpp"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <variant>

using namespace mosaic::features::provisioning;

namespace {
template <typename... Expected> void expect(const Resync& result) {
    assert(result.count == sizeof...(Expected));
    size_t i = 0;
    ((assert(std::holds_alternative<Expected>(result.events[i++]))), ...);
}
}

int main() {
    // Everything up to the window start is stale; later events are fresh.
    WifiObservation observation;
    assert(observation.stale(0) && !observation.stale(1));
    observation.ignore_through(41);
    assert(observation.stale(40) && observation.stale(41) && !observation.stale(42));
    // Sequence numbers wrap; events just after the wrap are still newer.
    observation.ignore_through(UINT32_MAX - 1);
    assert(observation.stale(UINT32_MAX - 1) && !observation.stale(UINT32_MAX));
    assert(!observation.stale(0) && !observation.stale(5) && observation.stale(UINT32_MAX - 100));

    // Only joining the saved network insists on that network.
    assert(needs_saved_network(Phase::saved_sta));
    for (const auto phase : { Phase::online, Phase::offline, Phase::opening_portal, Phase::unstarted })
        assert(!needs_saved_network(phase));

    // Phases that follow the station learn what the radio reports, in event order.
    for (const auto phase : { Phase::saved_sta, Phase::online, Phase::offline }) {
        assert(follows_station(phase));
        expect<Connected, GotIp>(resync(phase, { .connected = true, .has_ip = true }));
        expect<Connected, Disconnected>(resync(phase, { .connected = true, .has_ip = false }));
        expect<Disconnected>(resync(phase, { .connected = false, .has_ip = false }));
        // An address without an association is not a connection.
        expect<Disconnected>(resync(phase, { .connected = false, .has_ip = true }));
    }
    // Setup and startup do not follow the station, so nothing is replayed.
    for (const auto phase : { Phase::unstarted, Phase::starting_saved, Phase::opening_portal }) {
        assert(!follows_station(phase));
        expect<>(resync(phase, { .connected = true, .has_ip = true }));
    }
}
