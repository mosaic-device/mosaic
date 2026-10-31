#pragma once
#include "../presentation/view_state.hpp"
#include "../presentation/ui_action.hpp"
#include "../../../ui/runtime.hpp"
#include <string_view>
namespace mosaic::features::provisioning {
// Static feature renderer and mailbox live for the process lifetime.
mosaic::ui::Renderer renderer_callbacks();
// LVGL context. Whether the visible screen is the startup screen, and whether it is
// the online (unpaired) screen with setup closed.
bool starting();
bool online();
esp_err_t publish_view(const ViewState& state);
esp_err_t bind_action(ActionCallback callback, void* context);
esp_err_t render_setup_credentials(
    std::string_view ssid, std::string_view password, std::string_view url, uint32_t session);
}
