#pragma once

#include <span>

namespace mosaic::features::debug_triggers {

// A development command; run() returns 0 on success, like a console command.
struct Command {
    const char* name;
    const char* help;
    const char* hint;
    int (*run)(int argc, char** argv);
};

// `trigger <event>` publishes through the device event bus exactly as the M3 connection
// will; `volume` and `mute` show or change the saved cue settings.
std::span<const Command> commands();

// Splits line in place on spaces and tabs and runs the named command, or prints the
// list of commands. Returns its result, 0 for an empty line, or 1 when it is unknown.
// For consoles without their own command parser.
int execute(char* line, std::span<const Command> available = commands());

} // namespace mosaic::features::debug_triggers
