#include "screens.hpp"
#include "../events/bus.hpp"
#include "../features/home/screen.hpp"
#include "../features/led_feedback/service.hpp"
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
            // Until M3 stores the pairing itself, connecting proves it.
            .paired = events::paired(),
        };
    }
    // Any touch, turn or press means the owner is at the device and has seen what is waiting.
    void input(void*) {
        features::led_feedback::acknowledge();
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
        .input = input,
        .input_context = nullptr,
    });
}
}
