#include "screen.hpp"
#include "../clock/service.hpp"
#include "../../ui/components/controls.hpp"
#include "../../ui/generated/design_tokens.h"
#include <array>
#include <ctime>
#include <string>
namespace mosaic::features::home {
namespace {
    namespace components = ui::components;
    namespace styles = ui::styles;

    lv_obj_t* s_time = nullptr;
    std::time_t s_checked = -1; // The last second the clock text was checked.
    std::string s_shown;

    void open_settings(components::Request, void*) {
        ui::navigation::open(ui::navigation::Screen::settings);
    }
    bool create(lv_obj_t* container, void*) {
        lv_obj_set_style_pad_row(container, TOKEN_FIRMWARE_SPACE_GAP, 0);
        std::array<char, clock::kTextSize> buffer;
        s_shown = clock::now_text(buffer);
        s_time = components::text(container, s_shown.c_str(), styles::Text::title);
        lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, 0);
        auto* settings
            = components::button(container, "Settings", styles::Control::secondary, { .callback = open_settings });
        lv_obj_set_width(settings, lv_pct(50));
        lv_obj_set_style_text_align(settings, LV_TEXT_ALIGN_CENTER, 0);
        return s_time && settings;
    }
    // Every UI tick; checks the clock once a second and redraws only when it changes.
    void update(void*) {
        const std::time_t now = std::time(nullptr);
        if (now == s_checked)
            return;
        s_checked = now;
        std::array<char, clock::kTextSize> buffer;
        const std::string_view text = clock::now_text(buffer);
        if (text == s_shown)
            return;
        s_shown = text;
        lv_label_set_text(s_time, s_shown.c_str());
    }
}
ui::navigation::Page page() {
    return { .create = create, .update = update, .context = nullptr };
}
}
