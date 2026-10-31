#pragma once
#include "../../ui/navigation/host.hpp"
namespace mosaic::features::settings {
// Device settings, opened from Home: automatic or manual brightness, volume and
// mute, and LEDs on or off. Back returns there.
ui::navigation::Page page();
}
