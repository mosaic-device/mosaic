#pragma once
namespace mosaic::features::provisioning {
enum class UiAction { SetupTapped };
using ActionCallback = bool (*)(UiAction, void*);
}
