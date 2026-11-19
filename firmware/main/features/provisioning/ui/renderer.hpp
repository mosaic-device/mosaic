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
// Setup is open, opening or closing; see ViewState::setup. LVGL context.
bool setup_open();
// Setup recovery as the status screen offers it, for Settings on a paired device. LVGL
// context. `prompt` is the status screen's current prompt, kEnterSetup when nothing is in
// progress; `available` is false while setup can't be asked for, or the button is disabled.
struct Recovery {
    std::string_view prompt;
    bool available = false;
};
Recovery recovery();
// The same as pressing the status screen's recovery button. LVGL context.
void request_setup();
esp_err_t publish_view(const ViewState& state);
esp_err_t bind_action(ActionCallback callback, void* context);
esp_err_t render_setup_credentials(
    std::string_view ssid, std::string_view password, std::string_view url, uint32_t session);
}
