#pragma once
// Preview controls for the host platform, beyond the IDF APIs it stands in for.
#include <filesystem>

namespace mosaic::preview::platform {

// What esp_restart() does after the shutdown handlers run. It must not return; the
// default exits.
void set_restart(void (*restart)());

// Where NVS lives. Call before nvs_flash_init().
void set_nvs_file(const std::filesystem::path& path);
// While set, every NVS write fails as a full partition would, so storage errors can be
// reproduced.
void set_nvs_write_failure(bool failing);
bool nvs_write_failure();
// Erases every namespace, in memory and on disk.
void erase_nvs();

} // namespace mosaic::preview::platform
