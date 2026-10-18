#include "captive_portal.hpp"
#include "../../../support/secure_erase.hpp"
#include "dns_lifecycle.hpp"
#include "../settings_validation.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_js_start[] asm("_binary_setup_js_start");

namespace mosaic::features::provisioning::adapters::portal {
namespace {
    constexpr size_t kBodyLimit = form::kEncodedBodyLimit;
    constexpr int64_t kBodyReceiveLimitUs = 10'000'000;
    constexpr int64_t kAttemptIntervalUs = 3'000'000;
    Context context { };
    httpd_handle_t server = nullptr;
    DnsLifecycle dns;
    int dns_socket = -1; // Published by task creation; only the worker uses/closes it afterward.
    std::atomic_bool pending { false };
    std::atomic<int64_t> last_attempt { 0 };
    bool socket_ipv4(const sockaddr_storage& address, socklen_t length, uint32_t& result) {
        if (address.ss_family == AF_INET && length >= sizeof(sockaddr_in)) {
            result = reinterpret_cast<const sockaddr_in*>(&address)->sin_addr.s_addr;
            return true;
        }
#if CONFIG_LWIP_IPV6
        // ESP-IDF's HTTP server uses an IPv6 socket when IPv6 is enabled; lwIP
        // reports IPv4 connections to that socket as IPv4-mapped IPv6 addresses.
        if (address.ss_family == AF_INET6 && length >= sizeof(sockaddr_in6)) {
            const auto& ipv6 = reinterpret_cast<const sockaddr_in6*>(&address)->sin6_addr;
            if (IN6_IS_ADDR_V4MAPPED(&ipv6)) {
                std::memcpy(&result, &ipv6.s6_addr[12], sizeof(result));
                return true;
            }
        }
#endif
        return false;
    }

    // Only a connection addressed to the AP's own IP may access the portal.
    // esp_http_server listens on all interfaces, so retain this check in AP-only setup.
    bool from_ap(httpd_req_t* request) {
        const int fd = httpd_req_to_sockfd(request);
        sockaddr_storage local { }, remote { };
        uint32_t local_ip = 0, remote_ip = 0;
        socklen_t len = sizeof(local);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &len) < 0 || !socket_ipv4(local, len, local_ip)
            || local_ip != inet_addr(context.ip))
            return false;
        len = sizeof(remote);
        if (getpeername(fd, reinterpret_cast<sockaddr*>(&remote), &len) < 0 || !socket_ipv4(remote, len, remote_ip))
            return false;
        esp_netif_ip_info_t info { };
        if (esp_netif_get_ip_info(context.ap_netif, &info) != ESP_OK)
            return false;
        return (remote_ip & info.netmask.addr) == (info.ip.addr & info.netmask.addr);
    }

    esp_err_t page(httpd_req_t* req) {
        if (!from_ap(req))
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Setup AP only");
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "Content-Security-Policy",
            "default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; connect-src 'self'; form-action 'self'");
        return httpd_resp_sendstr(req, setup_html_start);
    }

    esp_err_t script(httpd_req_t* req) {
        if (!from_ap(req))
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Setup AP only");
        httpd_resp_set_type(req, "text/javascript; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_sendstr(req, setup_js_start);
    }

    bool same_host(httpd_req_t* req) {
        char expected[32];
        std::snprintf(expected, sizeof(expected), "%s", context.ip);
        char value[256] { };
        size_t len = httpd_req_get_hdr_value_len(req, "Host");
        if (len != std::strlen(expected) || len >= sizeof(value)
            || httpd_req_get_hdr_value_str(req, "Host", value, sizeof(value)) != ESP_OK
            || std::strcmp(value, expected) != 0)
            return false;
        len = httpd_req_get_hdr_value_len(req, "Origin");
        if (len) {
            if (len >= sizeof(value) || httpd_req_get_hdr_value_str(req, "Origin", value, sizeof(value)) != ESP_OK)
                return false;
            char origin[40];
            std::snprintf(origin, sizeof(origin), "http://%s", context.ip);
            if (std::strcmp(value, origin) != 0)
                return false;
        }
        return true;
    }

    esp_err_t status(httpd_req_t* req) {
        if (!from_ap(req))
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Setup AP only");
        char accept[64] { };
        const bool json = httpd_req_get_hdr_value_str(req, "Accept", accept, sizeof(accept)) == ESP_OK
            && std::strstr(accept, "application/json") != nullptr;
        httpd_resp_set_type(req, json ? "application/json" : "text/plain; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        // Submission may be queued before the owner publishes its saving state.
        // This is transport admission, not a second authoritative feature state.
        const char* plain = "Ready for setup.";
        const char* structured = "{\"state\":\"ready\"}";
        const PortalStatus state = pending.load() ? PortalStatus::saving : context.status->load();
        switch (state) {
        case PortalStatus::saving:
            plain = "Saving settings. Refresh in a moment.";
            structured = "{\"state\":\"saving\"}";
            break;
        case PortalStatus::saved:
            plain = "Wi-Fi saved. Mosaic will reboot in about six seconds, then try connecting. Device pairing is "
                    "still required.";
            structured = "{\"state\":\"saved\"}";
            break;
        case PortalStatus::storage_error:
            plain = "Saving failed. Settings were not confirmed saved. Retry setup.";
            structured = "{\"state\":\"storage_error\"}";
            break;
        default:
            break;
        }
        return httpd_resp_sendstr(req, json ? structured : plain);
    }

    esp_err_t save(httpd_req_t* req) {
        if (!from_ap(req) || !same_host(req))
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Use the displayed URL");
        const PortalStatus state = context.status->load();
        if (pending.load() || state == PortalStatus::saving || state == PortalStatus::saved) {
            httpd_resp_set_status(req, "409 Conflict");
            return httpd_resp_sendstr(req, "Setup already in progress");
        }
        if (req->content_len <= 0 || req->content_len > kBodyLimit)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form size");
        char type[64] { };
        if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK
            || std::strcmp(type, "application/x-www-form-urlencoded") != 0)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected form data");
        int64_t now = esp_timer_get_time();
        int64_t previous = last_attempt.load();
        if ((previous != 0 && now - previous < kAttemptIntervalUs)
            || !last_attempt.compare_exchange_strong(previous, now)) {
            httpd_resp_set_status(req, "429 Too Many Requests");
            return httpd_resp_sendstr(req, "Wait a few seconds before retrying");
        }
        char body[kBodyLimit + 1] { };
        Settings settings { };
        int received = 0;
        const int64_t receive_deadline = esp_timer_get_time() + kBodyReceiveLimitUs;
        while (received < req->content_len) {
            if (esp_timer_get_time() >= receive_deadline) {
                support::secure_erase(body, sizeof(body));
                return ESP_FAIL;
            }
            int n = httpd_req_recv(req, body + received, req->content_len - received);
            if (n == HTTPD_SOCK_ERR_TIMEOUT) {
                support::secure_erase(body, sizeof(body));
                return ESP_FAIL;
            }
            if (n <= 0) {
                support::secure_erase(body, sizeof(body));
                return ESP_FAIL;
            }
            received += n;
        }
        bool ok = form::parse(std::string_view(body, received), settings);
        support::secure_erase(body, sizeof(body));
        if (!ok) {
            support::secure_erase(&settings, sizeof(settings));
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID, password or WSS URL");
        }
        if (pending.exchange(true)) {
            support::secure_erase(&settings, sizeof(settings));
            httpd_resp_set_status(req, "409 Conflict");
            return httpd_resp_sendstr(req, "Setup already in progress");
        }
        const bool queued = context.submit(settings, context.user);
        support::secure_erase(&settings, sizeof(settings));
        if (!queued) {
            pending.store(false);
            httpd_resp_set_status(req, "503 Service Unavailable");
            return httpd_resp_sendstr(req, "Busy; retry shortly");
        }
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_sendstr(req,
            "<p>Saving settings without testing Wi-Fi. <a href='/status'>Check status</a> or watch the device "
            "display.</p>");
    }

    esp_err_t redirect(httpd_req_t* req) {
        if (!from_ap(req))
            return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Setup AP only");
        char location[40];
        std::snprintf(location, sizeof(location), "http://%s/", context.ip);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", location);
        return httpd_resp_send(req, nullptr, 0);
    }

    void dns_loop(void*) {
        uint8_t packet[512];
        while (dns.running()) {
            sockaddr_in peer { };
            socklen_t length = sizeof(peer);
            int count = recvfrom(dns_socket, packet, sizeof(packet), 0, reinterpret_cast<sockaddr*>(&peer), &length);
            if (count < 17 || !dns.running())
                continue;
            // One uncompressed IN/A question only; reject malformed and truncated packets.
            if ((packet[2] & 0xf8) != 0 || packet[3] != 0 || packet[4] != 0 || packet[5] != 1 || packet[6] || packet[7]
                || packet[8] || packet[9] || packet[10] || packet[11])
                continue;
            int pos = 12;
            while (pos < count && packet[pos] && !(packet[pos] & 0xc0) && packet[pos] <= 63) {
                if (pos + 1 + packet[pos] >= count)
                    break;
                pos += 1 + packet[pos];
            }
            if (pos + 5 != count || packet[pos] != 0 || packet[pos + 1] != 0 || packet[pos + 2] != 1
                || packet[pos + 3] != 0 || packet[pos + 4] != 1 || count + 16 > static_cast<int>(sizeof(packet)))
                continue;
            packet[2] = 0x81;
            packet[3] = 0x80;
            packet[6] = 0;
            packet[7] = 1;
            int answer = count;
            packet[answer++] = 0xc0;
            packet[answer++] = 0x0c;
            packet[answer++] = 0;
            packet[answer++] = 1;
            packet[answer++] = 0;
            packet[answer++] = 1;
            packet[answer++] = 0;
            packet[answer++] = 0;
            packet[answer++] = 0;
            packet[answer++] = 30;
            packet[answer++] = 0;
            packet[answer++] = 4;
            in_addr address { .s_addr = dns.address() };
            std::memcpy(packet + answer, &address.s_addr, 4);
            answer += 4;
            sendto(dns_socket, packet, answer, 0, reinterpret_cast<sockaddr*>(&peer), length);
        }
        close(dns_socket);
        dns_socket = -1;
        // Final access to owned state. After publication only self-deletion remains.
        dns.complete();
        vTaskDelete(nullptr);
    }

    bool start_dns() {
        // A previous worker retains ownership until its final completion publication.
        if (!dns.reap()) {
            ESP_LOGE("mosaic_network", "Previous DNS task is still stopping");
            return false;
        }
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0) {
            ESP_LOGE("mosaic_network", "DNS socket creation failed: errno %d", errno);
            return false;
        }
        timeval timeout { .tv_sec = 1, .tv_usec = 0 };
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            ESP_LOGE("mosaic_network", "DNS receive timeout unavailable: errno %d", errno);
            close(fd);
            return false;
        }
        sockaddr_in bind_to { };
        bind_to.sin_family = AF_INET;
        bind_to.sin_port = htons(53);
        bind_to.sin_addr.s_addr = inet_addr(context.ip);
        if (bind(fd, reinterpret_cast<sockaddr*>(&bind_to), sizeof(bind_to)) < 0) {
            ESP_LOGE("mosaic_network", "DNS bind failed: errno %d", errno);
            close(fd);
            return false;
        }
        dns.begin(bind_to.sin_addr.s_addr);
        dns_socket = fd;
        if (xTaskCreate(dns_loop, "mosaic_dns", 3072, nullptr, 4, nullptr) != pdPASS) {
            ESP_LOGE("mosaic_network", "DNS task creation failed");
            close(fd);
            dns_socket = -1;
            dns.creation_failed();
            return false;
        }
        return true;
    }

    bool stop_portal() {
        dns.request_stop();
        if (server) {
            if (httpd_stop(server) != ESP_OK) {
                ESP_LOGE("mosaic_network", "HTTP server did not stop; retain portal dependencies");
                return false;
            }
            server = nullptr;
        }
        const TickType_t started = xTaskGetTickCount();
        while (!dns.reap()) {
            if (static_cast<TickType_t>(xTaskGetTickCount() - started) >= pdMS_TO_TICKS(2500)) {
                ESP_LOGE("mosaic_network", "DNS worker did not stop within 2.5 seconds");
                pending.store(false);
                return false;
            }
            vTaskDelay(1);
        }
        context = { };
        pending.store(false);
        return true;
    }

    bool start_http() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers = 10;
        config.recv_wait_timeout = 5;
        config.send_wait_timeout = 5;
        const esp_err_t start_err = httpd_start(&server, &config);
        if (start_err != ESP_OK) {
            ESP_LOGE("mosaic_network", "HTTP start failed: %s", esp_err_to_name(start_err));
            return false;
        }
        httpd_uri_t routes[] = {
            { .uri = "/", .method = HTTP_GET, .handler = page, .user_ctx = nullptr },
            { .uri = "/setup.js", .method = HTTP_GET, .handler = script, .user_ctx = nullptr },
            { .uri = "/save", .method = HTTP_POST, .handler = save, .user_ctx = nullptr },
            { .uri = "/status", .method = HTTP_GET, .handler = status, .user_ctx = nullptr },
            { .uri = "/generate_204", .method = HTTP_GET, .handler = redirect, .user_ctx = nullptr },
            { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = redirect, .user_ctx = nullptr },
            { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = redirect, .user_ctx = nullptr },
            { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = redirect, .user_ctx = nullptr },
        };
        for (auto& route : routes)
            if (httpd_register_uri_handler(server, &route) != ESP_OK) {
                ESP_LOGE("mosaic_network", "HTTP route registration failed: %s", route.uri);
                stop_portal();
                return false;
            }
        return true;
    }
} // namespace

bool start(const Context& options) {
    if (server || !dns.reap() || !options.ap_netif || !options.status || !options.submit || !options.ip)
        return false;
    context = options;
    pending.store(false);
    if (!start_http())
        return false;
    if (!start_dns()) {
        stop_portal();
        return false;
    }
    return true;
}
bool stop() {
    return stop_portal();
}
void consumed() {
    pending.store(false);
}
} // namespace mosaic::features::provisioning::adapters::portal
