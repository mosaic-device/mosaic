#include "console_log.hpp"

#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define write _write
#define read _read
#define close _close
#else
#include <unistd.h>
#endif

namespace mosaic::preview::console_log {
namespace {
    constexpr size_t kHistory = 200;
    std::mutex g_lock;
    std::deque<std::string> g_lines;
    std::string g_partial;
    int g_terminal = -1;

    void keep(const char* bytes, size_t size) {
        const std::lock_guard lock(g_lock);
        for (size_t i = 0; i < size; ++i) {
            if (bytes[i] == '\n') {
                g_lines.push_back(std::move(g_partial));
                g_partial.clear();
                if (g_lines.size() > kHistory)
                    g_lines.pop_front();
            } else if (bytes[i] != '\r') {
                g_partial += bytes[i];
            }
        }
    }
} // namespace

void start() {
    int ends[2];
#ifdef _WIN32
    if (_pipe(ends, 4096, _O_BINARY) != 0)
        return;
#else
    if (pipe(ends) != 0)
        return;
#endif
    std::fflush(stdout);
    g_terminal = dup(1);
    if (g_terminal < 0 || dup2(ends[1], 1) < 0) {
        close(ends[0]);
        close(ends[1]);
        return;
    }
    close(ends[1]);
    // Unbuffered, so replies show as soon as they are printed.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const int source = ends[0];
    std::thread([source] {
        char buffer[1024];
        while (true) {
            const auto count = read(source, buffer, sizeof(buffer));
            if (count <= 0)
                return;
            (void)!write(g_terminal, buffer, static_cast<unsigned>(count));
            keep(buffer, static_cast<size_t>(count));
        }
    }).detach();
}

void stop() {
    if (g_terminal < 0)
        return;
    std::fflush(stdout);
    dup2(g_terminal, 1);
}

std::vector<std::string> lines(size_t count) {
    const std::lock_guard lock(g_lock);
    const size_t first = g_lines.size() > count ? g_lines.size() - count : 0;
    std::vector<std::string> result(g_lines.begin() + static_cast<std::ptrdiff_t>(first), g_lines.end());
    if (!g_partial.empty())
        result.push_back(g_partial);
    if (result.size() > count)
        result.erase(result.begin());
    return result;
}

} // namespace mosaic::preview::console_log
