#pragma once
#include "../../ui/navigation/host.hpp"
namespace mosaic::features::home {
// Home: the time and the way into Settings. Shown once the device is paired.
// The time shows a placeholder until the clock has synced this boot.
ui::navigation::Page page();
}
