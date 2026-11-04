#include "commands.hpp"

#include "../../events/bus.hpp"
#include "../settings/service.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mosaic::features::debug_triggers {
namespace {
    namespace events = mosaic::events;

    int trigger(int argc, char** argv) {
        events::Status status;
        esp_err_t err;
        if (argc == 2 && std::strcmp(argv[1], "notification") == 0) {
            err = events::post_notification();
        } else if (argc == 2 && events::parse(argv[1], status)) {
            err = events::set_status(status);
        } else {
            std::printf("usage: trigger notification|pairing|connected|disconnected|error\n");
            return 1;
        }
        if (err != ESP_OK) {
            std::printf("not posted: %s\n", esp_err_to_name(err));
            return 1;
        }
        return 0;
    }

    int volume(int argc, char** argv) {
        if (argc == 2) {
            char* end = nullptr;
            const long percent = std::strtol(argv[1], &end, 10);
            if (*argv[1] == '\0' || *end != '\0' || percent < settings::kVolumeMin || percent > 100) {
                std::printf("usage: volume [10-100]\n");
                return 1;
            }
            // Set directly, unlike the screen, so it works while sounds are off too.
            auto current = settings::current();
            current.volume = static_cast<uint8_t>(percent);
            settings::set(current);
        } else if (argc != 1) {
            std::printf("usage: volume [10-100]\n");
            return 1;
        }
        std::printf("volume %u%%\n", settings::current().volume);
        return 0;
    }

    int mute(int argc, char** argv) {
        auto current = settings::current();
        if (argc == 2 && (std::strcmp(argv[1], "on") == 0 || std::strcmp(argv[1], "off") == 0)) {
            current.muted = std::strcmp(argv[1], "on") == 0;
            settings::set(current);
        } else if (argc != 1) {
            std::printf("usage: mute [on|off]\n");
            return 1;
        }
        std::printf("mute %s\n", settings::current().muted ? "on" : "off");
        return 0;
    }

    constexpr Command kCommands[] = {
        { "trigger", "Post a notification, or set status: pairing, connected, disconnected or error", "<event>",
            trigger },
        { "volume", "Show or set the saved cue volume (10-100)", "[10-100]", volume },
        { "mute", "Show or set the saved cue muting", "[on|off]", mute },
    };
} // namespace

std::span<const Command> commands() {
    return kCommands;
}

int execute(char* line, std::span<const Command> available) {
    // More words than this are ignored; no command takes more than one argument.
    std::array<char*, 8> argv { };
    int argc = 0;
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    for (char* at = line; *at && argc < static_cast<int>(argv.size());) {
        while (*at && blank(*at))
            *at++ = '\0';
        if (!*at)
            break;
        argv[argc++] = at;
        while (*at && !blank(*at))
            ++at;
        if (*at)
            *at++ = '\0';
    }
    if (argc == 0)
        return 0;
    for (const Command& command : available)
        if (std::strcmp(command.name, argv[0]) == 0)
            return command.run(argc, argv.data());
    std::printf("commands:\n");
    for (const Command& command : available)
        std::printf("  %s %s: %s\n", command.name, command.hint, command.help);
    return 1;
}

} // namespace mosaic::features::debug_triggers
