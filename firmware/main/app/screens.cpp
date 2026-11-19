#include "screens.hpp"
#include "../events/bus.hpp"
#include "../features/home/screen.hpp"
#include "../features/led_feedback/service.hpp"
#include "../features/provisioning/ui/renderer.hpp"
#include "../features/settings/screen.hpp"
#include "../ui/navigation/host.hpp"
#include "../ui/navigation/paired_hold.hpp"
namespace mosaic::app {
namespace {
    namespace provisioning = features::provisioning;

    ui::navigation::PairedHold s_paired_hold;

    ui::navigation::Conditions conditions(void*) {
        return {
            .starting = provisioning::starting(),
            .setup = provisioning::setup_open(),
            // Saved with the device credential, and changed only by the server. Held back for
            // a moment after pairing, while the pairing screen says it worked.
            .paired = s_paired_hold.paired(events::paired(), lv_tick_get()),
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
