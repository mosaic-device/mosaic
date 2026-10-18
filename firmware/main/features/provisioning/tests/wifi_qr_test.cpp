#include "../presentation/wifi_qr.hpp"

#include <cassert>
#include <cstring>

int main() {
    char payload[256] { };
    assert(
        mosaic::features::provisioning::wifi_join_payload("Mosaic-A1B2C3", "ABCD2345EFGH", payload, sizeof(payload)));
    assert(std::strcmp(payload, "WIFI:T:WPA;S:Mosaic-A1B2C3;P:ABCD2345EFGH;;") == 0);

    assert(mosaic::features::provisioning::wifi_join_payload("Mosaic;1", "back\\slash,:;", payload, sizeof(payload)));
    assert(std::strcmp(payload, "WIFI:T:WPA;S:Mosaic\\;1;P:back\\\\slash\\,\\:\\;;;") == 0);

    char short_buffer[8] { };
    assert(!mosaic::features::provisioning::wifi_join_payload(
        "Mosaic-A1B2C3", "ABCD2345EFGH", short_buffer, sizeof(short_buffer)));
    assert(short_buffer[0] == '\0');
    assert(!mosaic::features::provisioning::wifi_join_payload("", "ABCD2345EFGH", payload, sizeof(payload)));
}
