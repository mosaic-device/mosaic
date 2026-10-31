#include "screen.hpp"
#include "../../ui/components/controls.hpp"
#include "../../ui/generated/design_tokens.h"
namespace mosaic::features::home {
namespace {
    namespace components = ui::components;
    namespace styles = ui::styles;

    void open_settings(components::Request, void*) {
        ui::navigation::open(ui::navigation::Screen::settings);
    }
    bool create(lv_obj_t* container, void*) {
        lv_obj_set_style_pad_row(container, TOKEN_FIRMWARE_SPACE_GAP, 0);
        // The clock replaces this once time sync exists.
        auto* time = components::text(container, "--:--", styles::Text::title);
        lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_CENTER, 0);
        auto* settings
            = components::button(container, "Settings", styles::Control::secondary, { .callback = open_settings });
        lv_obj_set_width(settings, lv_pct(50));
        lv_obj_set_style_text_align(settings, LV_TEXT_ALIGN_CENTER, 0);
        return time && settings;
    }
}
ui::navigation::Page page() {
    return { .create = create, .update = nullptr, .context = nullptr };
}
}
