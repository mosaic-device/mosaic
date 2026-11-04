#include "../adapters/wifi.hpp"
#include "esp_event.h"
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
esp_netif_t ap_netif, sta_netif;
std::vector<void*> destroyed;
esp_event_handler_t wifi_handler, ip_handler;
int handler_token;
const char* associated_ssid;
std::vector<wifi::Event> received;

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
    destroyed.clear();
    wifi_handler = ip_handler = nullptr;
    associated_ssid = nullptr;
    received.clear();
}
void receive(wifi::Event event) {
    received.push_back(event);
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
esp_err_t esp_wifi_init(const wifi_init_config_t*) {
    return record("init");
}
esp_err_t esp_wifi_deinit() {
    return record("deinit");
}
esp_err_t esp_wifi_set_storage(wifi_storage_t storage) {
    assert(storage == WIFI_STORAGE_RAM);
    return record("storage");
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t* info) {
    if (!associated_ssid)
        return kError;
    std::strncpy(reinterpret_cast<char*>(info->ssid), associated_ssid, sizeof(info->ssid) - 1);
    return ESP_OK;
}
esp_netif_t* esp_netif_create_default_wifi_ap() {
    return record("ap_netif") == ESP_OK ? &ap_netif : nullptr;
}
esp_netif_t* esp_netif_create_default_wifi_sta() {
    return record("sta_netif") == ESP_OK ? &sta_netif : nullptr;
}
void esp_netif_destroy_default_wifi(void* netif) {
    destroyed.push_back(netif);
}
esp_err_t esp_event_handler_instance_register(
    esp_event_base_t base, int32_t id, esp_event_handler_t handler, void*, esp_event_handler_instance_t* instance) {
    const bool wifi_events = std::strcmp(base, WIFI_EVENT) == 0;
    assert(wifi_events ? id == ESP_EVENT_ANY_ID : id == IP_EVENT_STA_GOT_IP);
    const auto err = record(wifi_events ? "register_wifi" : "register_ip");
    if (err == ESP_OK) {
        (wifi_events ? wifi_handler : ip_handler) = handler;
        *instance = &handler_token;
    }
    return err;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t, esp_event_handler_instance_t instance) {
    assert(instance == &handler_token);
    return record(std::strcmp(base, WIFI_EVENT) == 0 ? "unregister_wifi" : "unregister_ip");
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
    assert(wifi::configure_ap(ap) == ESP_OK);
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
        assert(wifi::configure_ap(ap) == kError);
        assert(!radio_started);
        if (failing_call == "mode")
            expect({ "mode" });
        else
            expect({ "mode", "ap_config" });
    }
    reset();
    radio_started = false;
    ip_result = kError;
    assert(wifi::configure_ap(ap) == kError && !radio_started);
    reset();
    radio_started = false;
    ap_ip = 0;
    assert(wifi::configure_ap(ap) == ESP_FAIL && !radio_started);
    reset();
    radio_started = false;
    failing_call = "start";
    assert(wifi::start_ap() == kError && !radio_started);
    expect({ "start" });

    // Stopping a radio that never started is already stopped.
    reset();
    stop_result = ESP_ERR_WIFI_NOT_STARTED;
    assert(wifi::stop() == ESP_OK);
    reset();
    stop_result = kError;
    assert(wifi::stop() == kError);

    // Init creates both netifs, keeps credentials in RAM, then subscribes to station events.
    const std::vector<std::string> init_sequence { "ap_netif", "sta_netif", "init", "storage", "register_wifi",
        "register_ip" };
    const char* stage = nullptr;
    reset();
    assert(wifi::init(receive, stage) == ESP_OK);
    assert(calls == init_sequence && wifi::initialized() && wifi::ap_netif() == &ap_netif);
    wifi_handler(nullptr, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, nullptr);
    ip_handler(nullptr, IP_EVENT, IP_EVENT_STA_GOT_IP, nullptr);
    wifi_handler(nullptr, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, nullptr);
    wifi_handler(nullptr, WIFI_EVENT, 99, nullptr);
    assert((received == std::vector { wifi::Event::connected, wifi::Event::got_ip, wifi::Event::disconnected }));

    // Station queries require association, the requested network, and an address.
    associated_ssid = "saved-network";
    assert(wifi::associated(nullptr) && wifi::associated("saved-network") && !wifi::associated("other"));
    assert(wifi::has_ip("saved-network") && !wifi::has_ip("other"));
    ap_ip = 0;
    assert(!wifi::has_ip(nullptr));
    ap_ip = 0x0104A8C0;
    ip_result = kError;
    assert(!wifi::has_ip(nullptr));
    associated_ssid = nullptr;
    ip_result = ESP_OK;
    assert(!wifi::associated(nullptr) && !wifi::has_ip(nullptr));

    // A failed teardown step is retried without repeating the steps that finished.
    calls.clear();
    failing_call = "unregister_wifi";
    assert(wifi::deinit() == kError && wifi::initialized());
    expect({ "unregister_ip", "unregister_wifi" });
    calls.clear();
    failing_call.clear();
    assert(wifi::deinit() == ESP_OK && !wifi::initialized());
    expect({ "unregister_wifi", "deinit" });
    calls.clear();
    assert(wifi::deinit() == ESP_OK);
    expect({ });
    wifi::release();
    assert((destroyed == std::vector<void*> { &sta_netif, &ap_netif }) && !wifi::ap_netif());

    // A failed init reports its stage, and teardown undoes only the steps that ran.
    const std::vector<std::pair<std::string, std::string>> stages {
        { "ap_netif", "Wi-Fi netifs" },
        { "sta_netif", "Wi-Fi netifs" },
        { "init", "Wi-Fi init" },
        { "storage", "Wi-Fi init" },
        { "register_wifi", "Wi-Fi event registration" },
        { "register_ip", "IP event registration" },
    };
    for (size_t i = 0; i < stages.size(); ++i) {
        reset();
        failing_call = stages[i].first;
        const auto expected = failing_call.ends_with("netif") ? ESP_ERR_NO_MEM : kError;
        assert(wifi::init(receive, stage) == expected && stages[i].second == stage);
        calls.clear();
        failing_call.clear();
        assert(wifi::deinit() == ESP_OK);
        std::vector<std::string> undone;
        if (i > 4)
            undone.emplace_back("unregister_wifi");
        if (i > 2)
            undone.emplace_back("deinit");
        assert(calls == undone && !wifi::initialized());
        wifi::release();
        assert(destroyed.size() == (i < 2 ? 1u : 2u));
    }
}
