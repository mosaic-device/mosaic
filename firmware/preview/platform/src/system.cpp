#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mosaic_platform.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {
const auto g_boot = std::chrono::steady_clock::now();
std::mutex g_log_lock;
std::map<std::string, esp_log_level_t> g_levels;
std::vector<shutdown_handler_t> g_shutdown;
void (*g_restart)() = nullptr;

char letter(esp_log_level_t level) {
    switch (level) {
    case ESP_LOG_ERROR:
        return 'E';
    case ESP_LOG_WARN:
        return 'W';
    case ESP_LOG_INFO:
        return 'I';
    case ESP_LOG_DEBUG:
        return 'D';
    default:
        return 'V';
    }
}
} // namespace

const char* esp_err_to_name(esp_err_t code) {
    switch (code) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_NO_MEM:
        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE:
        return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED:
        return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_VERSION:
        return "ESP_ERR_INVALID_VERSION";
    case ESP_ERR_NVS_NOT_FOUND:
        return "ESP_ERR_NVS_NOT_FOUND";
    case ESP_ERR_NVS_READ_ONLY:
        return "ESP_ERR_NVS_READ_ONLY";
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
        return "ESP_ERR_NVS_NOT_ENOUGH_SPACE";
    case ESP_ERR_NVS_INVALID_HANDLE:
        return "ESP_ERR_NVS_INVALID_HANDLE";
    case ESP_ERR_NVS_INVALID_LENGTH:
        return "ESP_ERR_NVS_INVALID_LENGTH";
    case ESP_ERR_WIFI_NOT_STARTED:
        return "ESP_ERR_WIFI_NOT_STARTED";
    default:
        return "UNKNOWN ERROR";
    }
}

void mosaic_preview_error_check_failed(esp_err_t code, const char* expression, const char* file, int line) {
    std::fprintf(stderr, "ESP_ERROR_CHECK failed: %s (0x%x) at %s:%d\nexpression: %s\n", esp_err_to_name(code),
        static_cast<unsigned>(code), file, line, expression);
    std::abort();
}

void esp_log_level_set(const char* tag, esp_log_level_t level) {
    const std::lock_guard lock(g_log_lock);
    g_levels[tag] = level;
}

void esp_log_write(esp_log_level_t level, const char* tag, const char* format, ...) {
    const std::lock_guard lock(g_log_lock);
    const auto found = g_levels.find(tag);
    if (level > (found == g_levels.end() ? ESP_LOG_INFO : found->second))
        return;
    std::printf("%c (%lld) %s: ", letter(level), static_cast<long long>(esp_timer_get_time() / 1000), tag);
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::putchar('\n');
    std::fflush(stdout);
}

int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - g_boot).count();
}

esp_err_t esp_register_shutdown_handler(shutdown_handler_t handler) {
    if (!handler)
        return ESP_ERR_INVALID_ARG;
    const std::lock_guard lock(g_log_lock);
    g_shutdown.push_back(handler);
    return ESP_OK;
}

void esp_restart() {
    std::vector<shutdown_handler_t> handlers;
    {
        const std::lock_guard lock(g_log_lock);
        handlers = g_shutdown;
    }
    for (auto handler : handlers)
        handler();
    std::puts("Restarting");
    std::fflush(stdout);
    if (g_restart)
        g_restart();
    std::exit(0);
}

namespace mosaic::preview::platform {
void set_restart(void (*restart)()) {
    g_restart = restart;
}
}
