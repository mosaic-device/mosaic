#include "wifi_observation.hpp"

namespace mosaic::features::provisioning {

bool needs_saved_network(Phase phase) {
    return phase == Phase::saved_sta;
}

bool follows_station(Phase phase) {
    return phase == Phase::saved_sta || phase == Phase::online || phase == Phase::offline;
}

Resync resync(Phase phase, Radio radio) {
    Resync result;
    if (!follows_station(phase))
        return result;
    if (radio.connected)
        result.events[result.count++] = Connected { };
    if (radio.connected && radio.has_ip)
        result.events[result.count++] = GotIp { };
    else
        result.events[result.count++] = Disconnected { };
    return result;
}

} // namespace mosaic::features::provisioning
