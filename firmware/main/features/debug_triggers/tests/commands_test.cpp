#include "../commands.hpp"

#include "../../../events/bus.hpp"
#include "../../settings/service.hpp"

#include <cassert>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace events = mosaic::events;
namespace settings = mosaic::features::settings;
using mosaic::features::debug_triggers::Command;
using mosaic::features::debug_triggers::execute;

namespace {
std::optional<events::Status> posted_status;
int notifications = 0;
esp_err_t post_result = ESP_OK;
settings::Settings saved;
int saves = 0;

int run(const char* line) {
    std::string copy = line;
    return execute(copy.data());
}
void reset() {
    posted_status.reset();
    notifications = 0;
    post_result = ESP_OK;
    saved = { };
    saves = 0;
}
}

namespace mosaic::events {
esp_err_t set_status(Status status) {
    posted_status = status;
    return post_result;
}
esp_err_t post_notification() {
    ++notifications;
    return post_result;
}
}
namespace mosaic::features::settings {
Settings current() {
    return saved;
}
void set(const Settings& settings) {
    saved = settings;
    ++saves;
}
}

int main() {
    // Triggers publish through the bus like the server connection will.
    reset();
    assert(run("trigger notification") == 0 && notifications == 1);
    assert(run("  trigger\tconnected\r\n") == 0 && posted_status == events::Status::connected);
    assert(run("trigger error") == 0 && posted_status == events::Status::error);
    reset();
    assert(run("trigger") == 1 && run("trigger loud") == 1 && run("trigger connected now") == 1);
    assert(!posted_status && notifications == 0);
    post_result = ESP_FAIL;
    assert(run("trigger notification") == 1);

    // Volume accepts the settings range only, and works while muted.
    reset();
    saved.muted = true;
    assert(run("volume 55") == 0 && saved.volume == 55 && saved.muted && saves == 1);
    assert(run("volume") == 0 && saves == 1);
    const auto minimum = std::to_string(settings::kVolumeMin);
    assert(run(("volume " + minimum).c_str()) == 0 && saved.volume == settings::kVolumeMin);
    for (const char* bad : { "volume 101", "volume 9", "volume 5x", "volume -", "volume 1 2" })
        assert(run(bad) == 1);
    assert(saved.volume == settings::kVolumeMin && saves == 2);

    reset();
    assert(run("mute on") == 0 && saved.muted && run("mute off") == 0 && !saved.muted && saves == 2);
    assert(run("mute") == 0 && run("mute maybe") == 1 && saves == 2);

    // Empty lines do nothing; unknown commands list what exists.
    reset();
    assert(run("") == 0 && run(" \t\n") == 0);
    assert(run("reboot") == 1 && saves == 0 && !posted_status);

    // Any command table works, so other consoles can add their own.
    static int seen_argc = 0;
    static std::string seen_last;
    const Command extra[] = { { "echo", "Echo", "<words>", [](int argc, char** argv) {
                                   seen_argc = argc;
                                   seen_last = argv[argc - 1];
                                   return 7;
                               } } };
    std::string line = "echo a b c d e f g h i j";
    assert(execute(line.data(), extra) == 7 && seen_argc == 8 && seen_last == "g");
}
