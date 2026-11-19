#include "screen.hpp"
#include "service.hpp"
#include "../provisioning/presentation/presenter.hpp"
#include "../provisioning/ui/renderer.hpp"
#include "../../ui/components/controls.hpp"
#include "../../ui/generated/design_tokens.h"
#include <array>
#include <cstdio>
#include <string>
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
    // Setup recovery, for a paired device whose Wi-Fi or server changed: the same two presses
    // as on the setup screens.
    lv_obj_t* s_setup = nullptr;
    lv_obj_t* s_setup_label = nullptr;
    // A copy: the recovery's prompt points at provisioning's state, which changes in place.
    std::string s_shown_prompt;
    bool s_shown_available = false;
    bool s_setup_rendered = false;

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
    void open_setup(components::Request, void*) {
        provisioning::request_setup();
    }
    // The button names what it does until a press is in progress, then follows the prompt:
    // "Press again to confirm", "Opening setup..." and so on.
    void render_setup(const provisioning::Recovery& recovery) {
        const bool idle = recovery.prompt.empty() || recovery.prompt == provisioning::kEnterSetup;
        const std::string text = idle ? "Change Wi-Fi or server" : std::string(recovery.prompt);
        lv_label_set_text(s_setup_label, text.c_str());
        components::render(s_setup, { .disabled = !recovery.available });
        s_shown_prompt = recovery.prompt;
        s_shown_available = recovery.available;
        s_setup_rendered = true;
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
        s_setup = components::action_row(list, "", { .callback = open_setup });
        if (!s_setup)
            return false;
        s_setup_label = lv_obj_get_child(s_setup, 0);
        auto* back = components::back_button(header, { .callback = go_back });
        render(current());
        render_setup(provisioning::recovery());
        return back != nullptr && s_setup_label != nullptr;
    }
    // Every UI tick; redraws only when the settings change.
    void update(void*) {
        const Settings settings = current();
        if (!s_rendered || !(settings == s_shown))
            render(settings);
        const provisioning::Recovery recovery = provisioning::recovery();
        if (!s_setup_rendered || recovery.prompt != s_shown_prompt || recovery.available != s_shown_available)
            render_setup(recovery);
    }
}
ui::navigation::Page page() {
    return { .create = create, .update = update, .context = nullptr };
}
}
