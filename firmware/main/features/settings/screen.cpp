#include "screen.hpp"
#include "service.hpp"
#include "../../ui/components/controls.hpp"
#include "../../ui/generated/design_tokens.h"
#include <array>
#include <cstdio>
namespace mosaic::features::settings {
namespace {
    namespace components = ui::components;

    // Context for each control's callback, in screen order.
    constexpr std::array kControls {
        Control::auto_brightness,
        Control::brightness,
        Control::sounds,
        Control::volume,
        Control::leds,
    };
    constexpr const char* kLabels[] = {
        "Automatic brightness",
        "Brightness",
        "Sounds",
        "Volume",
        "LED lights",
    };
    // Switch rows, or empty for a level.
    std::array<lv_obj_t*, kControls.size()> s_switches { };
    // Level rows, or empty for a switch.
    std::array<components::Level, kControls.size()> s_levels { };
    Settings s_shown;
    bool s_rendered = false;

    const Control& control(size_t index) {
        return kControls[index];
    }
    void flip(components::Request, void* context) {
        settings::toggle(*static_cast<const Control*>(context));
    }
    void adjust(int step, void* context) {
        const Control which = *static_cast<const Control*>(context);
        const Range limits = range(which);
        settings::set_level(which, limits.min + step * limits.step);
    }
    void go_back(components::Request, void*) {
        ui::navigation::back();
    }
    // Shows the settings: switches on or off, levels and why any are unavailable.
    void render(const Settings& settings) {
        for (size_t i = 0; i < kControls.size(); ++i) {
            const Control which = control(i);
            if (is_switch(which)) {
                components::render_switch(s_switches[i], value(settings, which) != 0);
                continue;
            }
            const Range limits = range(which);
            const int level = value(settings, which);
            char text[8];
            std::snprintf(text, sizeof(text), "%d%%", level);
            components::render_level(
                s_levels[i], (level - limits.min) / limits.step, text, unavailable_reason(settings, which));
        }
        s_shown = settings;
        s_rendered = true;
    }
    bool create(lv_obj_t* container, void*) {
        auto* page = components::page(container);
        // Focus order follows creation: the encoder starts on the first setting and
        // reaches Back, created last, after the final one.
        auto* header = components::header(page, "Settings");
        auto* list = components::fill(page);
        lv_obj_set_style_pad_row(list, TOKEN_SPACE_3, 0);
        // The list scrolls, so it clips: leave room for the focus ring around each row.
        lv_obj_set_style_pad_all(list, TOKEN_FOCUS_OFFSET + TOKEN_FOCUS_WIDTH, 0);
        // Scrolls if the rows outgrow the screen; focus scrolls its control into view.
        lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
        for (size_t i = 0; i < kControls.size(); ++i) {
            auto* context = const_cast<Control*>(&control(i));
            if (is_switch(control(i))) {
                s_switches[i] = components::switch_row(list, kLabels[i], { .callback = flip, .context = context });
                if (!s_switches[i])
                    return false;
            } else {
                const Range limits = range(control(i));
                s_levels[i]
                    = components::level_row(list, kLabels[i], (limits.max - limits.min) / limits.step, adjust, context);
                if (!s_levels[i].slider)
                    return false;
            }
        }
        auto* back = components::back_button(header, { .callback = go_back });
        render(current());
        return back != nullptr;
    }
    // Every UI tick; redraws only when the settings change.
    void update(void*) {
        const Settings settings = current();
        if (!s_rendered || !(settings == s_shown))
            render(settings);
    }
}
ui::navigation::Page page() {
    return { .create = create, .update = update, .context = nullptr };
}
}
