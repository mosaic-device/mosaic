// The setup portal as a local web server. While the firmware has setup open, the real
// setup page is served on this computer, and its requests get the device's answers from
// the same rules. The device's captive DNS and setup-network checks have no equivalent
// here: there is no setup network to join.
#include "../device.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "features/provisioning/adapters/captive_portal.hpp"
#include "features/provisioning/portal_replies.hpp"
#include "features/provisioning/settings_validation.hpp"
#include "support/secure_erase.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
void close_socket(Socket socket) {
    closesocket(socket);
}
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket kNoSocket = -1;
void close_socket(Socket socket) {
    close(socket);
}
#endif

// The page and script the device embeds, generated from the same files at build time.
extern const unsigned char kSetupHtml[];
extern const unsigned char kSetupJs[];

namespace {
namespace provisioning = mosaic::features::provisioning;
constexpr const char* kTag = "mosaic_portal";
// The first free port from here is used.
constexpr int kFirstPort = 8080;
constexpr int kPortsToTry = 20;
constexpr size_t kHeaderLimit = 8192;
constexpr auto kReceiveLimit = std::chrono::seconds(10);

std::mutex g_lock;
bool g_open = false;
provisioning::adapters::portal::Context g_context { };
std::atomic_bool g_pending { false };
provisioning::AttemptThrottle g_throttle;
Socket g_listener = kNoSocket;
int g_port = 0;
std::string g_lan_address;

// The device's save handler after its network checks: screen, read, parse, hand off.
provisioning::HttpReply save(std::string_view body, int64_t content_length, std::string_view content_type) {
    const std::lock_guard lock(g_lock);
    if (!g_open)
        return { 503, "Setup is not open" };
    if (const auto rejected = provisioning::screen_save(
            g_context.status->load(), g_pending.load(), content_length, content_type, g_throttle, esp_timer_get_time()))
        return *rejected;
    provisioning::form::Settings settings { };
    if (!provisioning::form::parse(body, settings)) {
        mosaic::support::secure_erase(&settings, sizeof(settings));
        return provisioning::kInvalidSettings;
    }
    if (g_pending.exchange(true)) {
        mosaic::support::secure_erase(&settings, sizeof(settings));
        return provisioning::kInProgress;
    }
    const bool queued = g_context.submit(settings, g_context.user);
    mosaic::support::secure_erase(&settings, sizeof(settings));
    if (!queued) {
        g_pending = false;
        return provisioning::kBusy;
    }
    return provisioning::kAccepted;
}

void send_all(Socket client, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const auto count = send(client, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (count <= 0)
            return;
        sent += static_cast<size_t>(count);
    }
}

void respond(Socket client, const char* status, const char* type, std::string_view body, const char* extra = "") {
    std::string response = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type
        + "\r\nCache-Control: no-store\r\nConnection: close\r\n" + extra
        + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    response.append(body);
    send_all(client, response);
}

std::string header(std::string_view headers, std::string_view name) {
    for (size_t at = 0; at < headers.size();) {
        const size_t end = std::min(headers.find("\r\n", at), headers.size());
        const auto line = headers.substr(at, end - at);
        const size_t colon = line.find(':');
        if (colon != line.npos && colon == name.size()
            && std::equal(name.begin(), name.end(), line.begin(), [](char a, char b) {
                   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
               })) {
            auto value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ')
                value.remove_prefix(1);
            return std::string(value);
        }
        at = end + 2;
    }
    return { };
}

void serve(Socket client) {
    // Read the request head, then as much body as it declares, within the device's limits.
    std::string request;
    char buffer[2048];
    const auto deadline = std::chrono::steady_clock::now() + kReceiveLimit;
    size_t head_end = std::string::npos;
    while ((head_end = request.find("\r\n\r\n")) == std::string::npos) {
        if (request.size() > kHeaderLimit || std::chrono::steady_clock::now() > deadline)
            return;
        const auto count = recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0)
            return;
        request.append(buffer, static_cast<size_t>(count));
    }
    const std::string_view head(request.data(), head_end);
    const size_t line_end = head.find("\r\n");
    const auto request_line = head.substr(0, line_end);
    const auto headers = line_end == head.npos ? std::string_view { } : head.substr(line_end + 2);
    const size_t space = request_line.find(' ');
    const auto method = request_line.substr(0, space);
    auto path = space == request_line.npos ? std::string_view { } : request_line.substr(space + 1);
    path = path.substr(0, path.find(' '));
    path = path.substr(0, path.find('?'));

    if (method == "GET" && path == "/") {
        respond(client, "200 OK", "text/html; charset=utf-8", reinterpret_cast<const char*>(kSetupHtml),
            "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; connect-src "
            "'self'; form-action 'self'\r\n");
    } else if (method == "GET" && path == "/setup.js") {
        respond(client, "200 OK", "text/javascript; charset=utf-8", reinterpret_cast<const char*>(kSetupJs));
    } else if (method == "GET" && path == "/status") {
        const bool json = header(headers, "Accept").find("application/json") != std::string::npos;
        std::string reply;
        {
            const std::lock_guard lock(g_lock);
            const auto status = provisioning::status_reply(g_context.status->load(), g_pending.load());
            reply = json ? status.structured : status.plain;
        }
        respond(client, "200 OK", json ? "application/json" : "text/plain; charset=utf-8", reply);
    } else if (method == "POST" && path == "/save") {
        const std::string length_text = header(headers, "Content-Length");
        const int64_t length = length_text.empty() ? 0 : std::strtoll(length_text.c_str(), nullptr, 10);
        std::string body = request.substr(head_end + 4);
        // Only read a body the rules would look at.
        if (length > 0 && length <= static_cast<int64_t>(provisioning::kFormBodyLimit))
            while (static_cast<int64_t>(body.size()) < length && std::chrono::steady_clock::now() < deadline) {
                const auto count = recv(client, buffer, sizeof(buffer), 0);
                if (count <= 0)
                    break;
                body.append(buffer, static_cast<size_t>(count));
            }
        if (length > 0 && static_cast<int64_t>(body.size()) < length) {
            mosaic::support::secure_erase(body.data(), body.size());
            return; // The device drops a body that never arrives.
        }
        body.resize(static_cast<size_t>(std::max<int64_t>(length, 0)));
        const auto reply = save(body, length, header(headers, "Content-Type"));
        mosaic::support::secure_erase(body.data(), body.size());
        mosaic::support::secure_erase(request.data(), request.size());
        respond(client, provisioning::status_line(reply.code),
            reply.html ? "text/html; charset=utf-8" : "text/plain; charset=utf-8", reply.text);
    } else if (method == "GET"
        && (path == "/generate_204" || path == "/hotspot-detect.html" || path == "/connecttest.txt"
            || path == "/ncsi.txt")) {
        respond(client, "302 Found", "text/plain", "", "Location: /\r\n");
    } else {
        respond(client, "404 Not Found", "text/plain", "Not found");
    }
}

// The address other devices on this network reach this computer at, found by asking
// which interface would route outward. Nothing is sent.
std::string lan_address() {
    const Socket probe = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe == kNoSocket)
        return { };
    sockaddr_in target { };
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    inet_pton(AF_INET, "192.0.2.1", &target.sin_addr);
    std::string result;
    sockaddr_in local { };
    socklen_t size = sizeof(local);
    if (connect(probe, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0
        && getsockname(probe, reinterpret_cast<sockaddr*>(&local), &size) == 0) {
        char text[INET_ADDRSTRLEN] { };
        if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)) && std::strcmp(text, "0.0.0.0") != 0)
            result = text;
    }
    close_socket(probe);
    return result;
}

bool listen_on_free_port() {
#ifdef _WIN32
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!started)
        return false;
#endif
    for (int port = kFirstPort; port < kFirstPort + kPortsToTry; ++port) {
        const Socket listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener == kNoSocket)
            return false;
        // A restart replaces this process; the next one must not inherit the port.
#ifdef _WIN32
        SetHandleInformation(reinterpret_cast<HANDLE>(listener), HANDLE_FLAG_INHERIT, 0);
#else
        fcntl(listener, F_SETFD, FD_CLOEXEC);
        const int reuse = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
        sockaddr_in address { };
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        // Every interface, so a phone on the same network can open the page too.
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(listener, 8) == 0) {
            g_listener = listener;
            g_port = port;
            return true;
        }
        close_socket(listener);
    }
    return false;
}
} // namespace

namespace mosaic::features::provisioning::adapters::portal {
bool start(const Context& context) {
    const std::lock_guard lock(g_lock);
    if (g_open || !context.status || !context.submit || !context.ip)
        return false;
    g_context = context;
    g_pending = false;
    g_open = true;
    if (g_listener == kNoSocket) {
        if (!listen_on_free_port()) {
            ESP_LOGW(kTag, "No free port for the setup page; use the Phone button instead");
            return true;
        }
        g_lan_address = lan_address();
        const Socket listener = g_listener;
        std::thread([listener] {
            while (true) {
                const Socket client = accept(listener, nullptr, nullptr);
                if (client == kNoSocket)
                    return;
                // One at a time, like the device's single HTTP task.
                serve(client);
                close_socket(client);
            }
        }).detach();
    }
    ESP_LOGI(kTag, "Setup page: http://localhost:%d/", g_port);
    if (!g_lan_address.empty())
        ESP_LOGI(kTag, "From a phone on this network: http://%s:%d/", g_lan_address.c_str(), g_port);
    return true;
}
bool stop() {
    const std::lock_guard lock(g_lock);
    // The listener stays for the next setup; requests meanwhile find setup closed.
    g_open = false;
    g_pending = false;
    return true;
}
void consumed() {
    g_pending = false;
}
} // namespace mosaic::features::provisioning::adapters::portal

namespace mosaic::preview::device {
std::string submit_setup(std::string_view form_body) {
    {
        const std::lock_guard lock(g_lock);
        if (!g_open)
            return "No setup network: the device is not showing setup";
    }
    const auto reply = save(form_body, static_cast<int64_t>(form_body.size()), provisioning::kFormType);
    return reply.html ? provisioning::status_line(reply.code) : std::to_string(reply.code) + " " + reply.text;
}

std::string setup_status() {
    const std::lock_guard lock(g_lock);
    if (!g_open)
        return "No setup network: the device is not showing setup";
    return provisioning::status_reply(g_context.status->load(), g_pending.load()).plain;
}

std::string setup_page() {
    const std::lock_guard lock(g_lock);
    return g_open && g_port ? "http://localhost:" + std::to_string(g_port) + "/" : "";
}

std::string setup_page_for_phone() {
    const std::lock_guard lock(g_lock);
    return g_open && g_port && !g_lan_address.empty() ? "http://" + g_lan_address + ":" + std::to_string(g_port) + "/"
                                                      : "";
}
} // namespace mosaic::preview::device
