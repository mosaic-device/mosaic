// The radio and the saved network. Station events go through the default event loop
// like the device's Wi-Fi driver posts them, so every listener sees the same order.
#include "../device.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "features/provisioning/adapters/wifi.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>

namespace {
constexpr const char* kTag = "mosaic_wifi";
ESP_EVENT_DEFINE_BASE(PREVIEW_WIFI_EVENT);
enum : int32_t { kConnected, kDisconnected };
// How long the simulated network takes to answer.
constexpr auto kAssociate = std::chrono::milliseconds(700);
constexpr auto kAddress = std::chrono::milliseconds(400);
constexpr auto kReject = std::chrono::milliseconds(1500);

std::mutex g_lock;
std::atomic<mosaic::preview::device::Network> g_network { mosaic::preview::device::Network::joins };
mosaic::features::provisioning::adapters::wifi::OnEvent g_listener = nullptr;
esp_event_handler_instance_t g_wifi_handler = nullptr, g_ip_handler = nullptr;
bool g_initialized = false;
// Each start or stop begins a new attempt; answers for older attempts are dropped.
uint64_t g_attempt = 0;
bool g_started = false;
std::string g_joining;
std::string g_joined;
bool g_has_ip = false;

void on_wifi(void*, esp_event_base_t, int32_t id, void*) {
    using mosaic::features::provisioning::adapters::wifi::Event;
    if (g_listener)
        g_listener(id == kConnected ? Event::connected : Event::disconnected);
}
void on_ip(void*, esp_event_base_t, int32_t, void*) {
    if (g_listener)
        g_listener(mosaic::features::provisioning::adapters::wifi::Event::got_ip);
}

// With g_lock held. Answers the attempt later, as the network would.
void answer(uint64_t attempt) {
    std::thread([attempt] {
        using mosaic::preview::device::Network;
        const Network network = g_network.load();
        if (network == Network::silent)
            return;
        std::this_thread::sleep_for(network == Network::joins ? kAssociate : kReject);
        {
            const std::lock_guard lock(g_lock);
            if (attempt != g_attempt)
                return;
            if (network == Network::rejects) {
                esp_event_post(PREVIEW_WIFI_EVENT, kDisconnected, nullptr, 0, portMAX_DELAY);
                return;
            }
            g_joined = g_joining;
            esp_event_post(PREVIEW_WIFI_EVENT, kConnected, nullptr, 0, portMAX_DELAY);
        }
        std::this_thread::sleep_for(kAddress);
        const std::lock_guard lock(g_lock);
        if (attempt != g_attempt || g_joined.empty())
            return;
        g_has_ip = true;
        esp_event_post(IP_EVENT, IP_EVENT_STA_GOT_IP, nullptr, 0, portMAX_DELAY);
    }).detach();
}
// With g_lock held.
void leave() {
    ++g_attempt;
    g_started = false;
    g_joined.clear();
    g_has_ip = false;
}
} // namespace

namespace mosaic::features::provisioning::adapters::wifi {
esp_err_t init(OnEvent on_event, const char*& stage) {
    g_listener = on_event;
    stage = "Wi-Fi event registration";
    esp_err_t err
        = esp_event_handler_instance_register(PREVIEW_WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, nullptr, &g_wifi_handler);
    if (err != ESP_OK)
        return err;
    stage = "IP event registration";
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, nullptr, &g_ip_handler);
    if (err == ESP_OK)
        g_initialized = true;
    return err;
}
esp_err_t deinit() {
    if (g_ip_handler && esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, g_ip_handler) == ESP_OK)
        g_ip_handler = nullptr;
    if (g_wifi_handler
        && esp_event_handler_instance_unregister(PREVIEW_WIFI_EVENT, ESP_EVENT_ANY_ID, g_wifi_handler) == ESP_OK)
        g_wifi_handler = nullptr;
    g_initialized = false;
    return ESP_OK;
}
void release() { }
bool initialized() {
    return g_initialized;
}
esp_netif_t* ap_netif() {
    return nullptr;
}
esp_err_t configure_sta(const form::Settings& settings) {
    const std::lock_guard lock(g_lock);
    g_joining = settings.ssid;
    return ESP_OK;
}
esp_err_t start_saved(const form::Settings& settings) {
    configure_sta(settings);
    const std::lock_guard lock(g_lock);
    leave();
    g_started = true;
    ESP_LOGI(kTag, "Joining \"%s\"", g_joining.c_str());
    answer(g_attempt);
    return ESP_OK;
}
esp_err_t configure_ap(Ap& ap) {
    const std::lock_guard lock(g_lock);
    leave();
    // The device names its AP after the last three bytes of its MAC.
    std::snprintf(ap.ssid, sizeof(ap.ssid), "Mosaic-%02X%02X%02X", 0x4F, 0x2A, 0x1C);
    constexpr char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    std::random_device random;
    std::uniform_int_distribution<size_t> pick(0, sizeof(kAlphabet) - 2);
    for (size_t i = 0; i < 12; ++i)
        ap.key[i] = kAlphabet[pick(random)];
    ap.key[12] = '\0';
    std::snprintf(ap.ip, sizeof(ap.ip), "192.168.4.1");
    return ESP_OK;
}
esp_err_t start_ap() {
    const std::lock_guard lock(g_lock);
    g_started = true;
    ESP_LOGI(kTag, "Setup network open");
    return ESP_OK;
}
esp_err_t reconnect(const form::Settings& saved) {
    return start_saved(saved);
}
esp_err_t stop() {
    const std::lock_guard lock(g_lock);
    leave();
    return ESP_OK;
}
bool associated(const char* ssid) {
    const std::lock_guard lock(g_lock);
    return !g_joined.empty() && (!ssid || g_joined == ssid);
}
bool has_ip(const char* ssid) {
    const std::lock_guard lock(g_lock);
    return g_has_ip && !g_joined.empty() && (!ssid || g_joined == ssid);
}
} // namespace mosaic::features::provisioning::adapters::wifi

namespace mosaic::preview::device {
void set_network(Network network) {
    g_network = network;
}
Network network() {
    return g_network.load();
}
void drop_network() {
    const std::lock_guard lock(g_lock);
    if (g_joined.empty())
        return;
    // The radio stays started and the station is free to rejoin, as after a router restart.
    ++g_attempt;
    g_joined.clear();
    g_has_ip = false;
    esp_event_post(PREVIEW_WIFI_EVENT, kDisconnected, nullptr, 0, portMAX_DELAY);
}
} // namespace mosaic::preview::device
