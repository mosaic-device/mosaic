#include "screens.hpp"
#include "../events/bus.hpp"
#include "../features/home/screen.hpp"
#include "../features/provisioning/ui/renderer.hpp"
#include "../features/settings/screen.hpp"
#include "../ui/navigation/host.hpp"
namespace mosaic::app {
namespace {
    namespace provisioning = features::provisioning;

    ui::navigation::Conditions conditions(void*) {
        return {
            .starting = provisioning::starting(),
            .online = provisioning::online(),
            // Connected implies paired until M3 stores the pairing itself.
            .paired = events::status() == events::Status::connected,
        };
    }
} // namespace
ui::Renderer screens() {
    return ui::navigation::renderer({
        .setup = provisioning::renderer_callbacks(),
        .home = features::home::page(),
        .settings = features::settings::page(),
        .notification = { },
        .conditions = conditions,
        .conditions_context = nullptr,
    });
}
}
