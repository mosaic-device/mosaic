#include "screen.hpp"
#include "../../ui/components/controls.hpp"
namespace mosaic::features::settings {
namespace {
    namespace components = ui::components;
    namespace styles = ui::styles;

    void go_back(components::Request, void*) {
        ui::navigation::back();
    }
    bool create(lv_obj_t* container, void*) {
        auto* card = components::card(container);
        lv_obj_set_width(card, lv_pct(85));
        auto* heading = components::text(card, "Settings", styles::Text::title);
        auto* empty
            = components::text(card, "Brightness, sound and LED settings will appear here.", styles::Text::body, true);
        // Touch's way back; a long press of the encoder does the same.
        auto* back = components::button(card, "Back", styles::Control::secondary, { .callback = go_back });
        return heading && empty && back;
    }
}
ui::navigation::Page page() {
    return { .create = create, .update = nullptr, .context = nullptr };
}
}
