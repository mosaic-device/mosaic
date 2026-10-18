#include "../adapters/wifi.hpp"
#include "esp_wifi.h"

#include <cassert>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace wifi = mosaic::features::provisioning::adapters::wifi;
using mosaic::features::provisioning::form::Settings;

namespace {
std::vector<std::string> calls;
std::string failing_call;
esp_err_t stop_result;
constexpr esp_err_t kError = 1234;
Settings saved;
bool radio_started;
wifi_mode_t configured_mode;
esp_err_t ip_result = ESP_OK;
uint32_t ap_ip = 0x0104A8C0;

esp_err_t record(const char* name) {
    calls.emplace_back(name);
    return failing_call == name ? kError : ESP_OK;
}
void reset() {
    calls.clear();
    failing_call.clear();
    stop_result = ESP_OK;
    radio_started = true;
    configured_mode = WIFI_MODE_STA;
    ip_result = ESP_OK;
    ap_ip = 0x0104A8C0;
}
void expect(std::initializer_list<std::string> expected) {
    assert(calls == std::vector<std::string>(expected));
}
}

esp_err_t esp_wifi_stop() {
    const auto result = record("stop");
    if (result != ESP_OK)
        return result;
    if (stop_result == ESP_OK || stop_result == ESP_ERR_WIFI_NOT_STARTED)
        radio_started = false;
    return stop_result;
}
esp_err_t esp_wifi_set_mode(wifi_mode_t mode) {
    assert(mode == WIFI_MODE_STA || mode == WIFI_MODE_AP);
    configured_mode = mode;
    return record("mode");
}
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t* config) {
    if (interface == WIFI_IF_AP) {
        assert(configured_mode == WIFI_MODE_AP && !radio_started);
        assert(std::memcmp(config->ap.ssid, "Mosaic-000000", 13) == 0);
        assert(config->ap.ssid_len == 13 && config->ap.max_connection == 4);
        assert(config->ap.authmode == WIFI_AUTH_WPA2_PSK);
        assert(std::memcmp(config->ap.password, "222222222222", 12) == 0);
        return record("ap_config");
    }
    assert(interface == WIFI_IF_STA && configured_mode == WIFI_MODE_STA);
    assert(!radio_started);
    assert(std::memcmp(config->sta.ssid, saved.ssid, sizeof(config->sta.ssid)) == 0);
    assert(std::memcmp(config->sta.password, saved.pass, sizeof(config->sta.password)) == 0);
    assert(config->sta.threshold.authmode == (saved.pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN));
    return record("config");
}
esp_err_t esp_wifi_start() {
    assert(!radio_started);
    const auto err = record("start");
    if (err == ESP_OK)
        radio_started = true;
    return err;
}
esp_err_t esp_wifi_connect() {
    assert(radio_started);
    return record("connect");
}
esp_err_t esp_wifi_disconnect() {
    return ESP_OK;
}
esp_err_t esp_efuse_mac_get_default(uint8_t* mac) {
    std::memset(mac, 0, 6);
    return ESP_OK;
}
void bootloader_random_enable() { }
void bootloader_random_disable() { }
uint32_t esp_random() {
    return 0;
}
esp_err_t esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t* info) {
    info->ip.addr = ap_ip;
    record("ip");
    return ip_result;
}
const char* esp_err_to_name(esp_err_t) {
    return "fake error";
}
void esp_restart() {
    assert(false && "unexpected restart");
}

int main() {
    std::strcpy(saved.ssid, "saved-network");
    std::strcpy(saved.pass, "saved-password");
    const Settings original = saved;
    for (const auto stopped : { ESP_OK, ESP_ERR_WIFI_NOT_STARTED }) {
        reset();
        stop_result = stopped;
        radio_started = stopped == ESP_OK;
        assert(wifi::reconnect(saved) == ESP_OK);
        expect({ "stop", "mode", "config", "start", "connect" });
        assert(std::memcmp(&saved, &original, sizeof(saved)) == 0);
        // Every subsequent reconnect resets even an active association.
        calls.clear();
        assert(wifi::reconnect(saved) == ESP_OK);
        expect({ "stop", "mode", "config", "start", "connect" });
        assert(std::memcmp(&saved, &original, sizeof(saved)) == 0);
    }
    const std::vector<std::string> sequence { "stop", "mode", "config", "start", "connect" };
    for (size_t i = 0; i < sequence.size(); ++i) {
        reset();
        failing_call = sequence[i];
        assert(wifi::reconnect(saved) == kError);
        assert(calls == std::vector<std::string>(sequence.begin(), sequence.begin() + i + 1));
        assert(std::memcmp(&saved, &original, sizeof(saved)) == 0);
    }
    reset();
    saved = { };
    std::memset(saved.ssid, 's', 32);
    std::memset(saved.pass, 'p', 64);
    const Settings full_length = saved;
    assert(wifi::reconnect(saved) == ESP_OK);
    expect({ "stop", "mode", "config", "start", "connect" });
    assert(std::memcmp(&saved, &full_length, sizeof(saved)) == 0);
    reset();
    std::memset(saved.pass, 0, sizeof(saved.pass));
    const Settings open_network = saved;
    assert(wifi::reconnect(saved) == ESP_OK);
    expect({ "stop", "mode", "config", "start", "connect" });
    assert(std::memcmp(&saved, &open_network, sizeof(saved)) == 0);
    // Configuration does not start the AP: the owner must pass the display gate first.
    reset();
    radio_started = false;
    wifi::Ap ap { };
    assert(wifi::configure_ap(nullptr, ap) == ESP_OK);
    expect({ "mode", "ap_config", "ip" });
    assert(!radio_started && configured_mode == WIFI_MODE_AP);
    assert(std::strcmp(ap.ssid, "Mosaic-000000") == 0);
    assert(std::strcmp(ap.key, "222222222222") == 0 && ap.ip[0]);
    assert(wifi::start_ap() == ESP_OK);
    expect({ "mode", "ap_config", "ip", "start" });
    for (const auto& failure : { "mode", "ap_config" }) {
        reset();
        radio_started = false;
        failing_call = failure;
        assert(wifi::configure_ap(nullptr, ap) == kError);
        assert(!radio_started);
        if (failing_call == "mode")
            expect({ "mode" });
        else
            expect({ "mode", "ap_config" });
    }
    reset();
    radio_started = false;
    ip_result = kError;
    assert(wifi::configure_ap(nullptr, ap) == kError && !radio_started);
    reset();
    radio_started = false;
    ap_ip = 0;
    assert(wifi::configure_ap(nullptr, ap) == ESP_FAIL && !radio_started);
    reset();
    radio_started = false;
    failing_call = "start";
    assert(wifi::start_ap() == kError && !radio_started);
    expect({ "start" });
}
