#pragma once
// Tees the process's stdout into a short history the window's console panel shows, so
// command replies and firmware logs appear in the window and the terminal alike.
#include <string>
#include <vector>

namespace mosaic::preview::console_log {

void start();
// Puts stdout back as it was. Call before replacing the process, so the next one does not
// write into a pipe nobody reads.
void stop();
// The last count lines, oldest first.
std::vector<std::string> lines(size_t count);

} // namespace mosaic::preview::console_log
