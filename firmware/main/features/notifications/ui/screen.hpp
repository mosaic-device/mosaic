#pragma once
#include "../../../ui/navigation/host.hpp"
namespace mosaic::features::notifications {
// The incoming notification and its detail, in one screen above Home or Settings. It opens
// itself when a notification is stored, and follows Flow.
ui::navigation::Page page();
}
