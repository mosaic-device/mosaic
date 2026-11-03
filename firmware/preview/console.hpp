#pragma once
#include <string>

namespace mosaic::preview {
// Runs one console line: the firmware's debug commands, then the preview's controls
// for the simulated room, network and phone. Any thread.
int run_command(std::string line);
} // namespace mosaic::preview
