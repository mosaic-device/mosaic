#include "screen.hpp"
#include "service.hpp"
#include "../../ui/components/controls.hpp"
#include "../../ui/generated/design_tokens.h"
#include <array>
#include <cstdio>
namespace mosaic::features::settings {
namespace {
    namespace components = ui::components;
    namespace styles = ui::styles;

    // Context for each control's callback: the change it asks for.
    constexpr std::array kChanges {
        Change::auto_brightness,
        Change::dimmer,
        Change::brighter,
        Change::quieter,
        Change::louder,
        Change::mute,
        Change::leds,
    };
    // Controls, indexed like kChanges.
    std::array<lv_obj_t*, kChanges.size()> s_controls { };
    lv_obj_t* s_brightness_value = nullptr;
    lv_obj_t* s_volume_value = nullptr;
    Settings s_shown;
    bool s_rendered = false;

    void request(components::Request, void* context) {
        settings::change(*static_cast<const Change*>(context));
    }
    void go_back(components::Request, void*) {
        ui::navigation::back();
    }
    components::Action action(size_t index) {
        return { .callback = request, .context = const_cast<Change*>(&kChanges[index]) };
    }
    lv_obj_t* row(lv_obj_t* parent) {
        auto* object = lv_obj_create(parent);
        lv_obj_remove_style_all(object);
        lv_obj_set_size(object, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(object, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(object, TOKEN_FIRMWARE_SPACE_GAP, 0);
        return object;
    }
    // A label with minus and plus buttons; returns the value label.
    lv_obj_t* stepper(lv_obj_t* parent, const char* name, size_t minus, size_t plus) {
        auto* line = row(parent);
        auto* label = components::text(line, name);
        lv_obj_set_flex_grow(label, 1);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        s_controls[minus] = components::button(line, "-", styles::Control::secondary, action(minus));
        auto* value = components::text(line, "");
        lv_obj_set_width(value, TOKEN_FIRMWARE_CONTROL_MD * 2);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
        s_controls[plus] = components::button(line, "+", styles::Control::secondary, action(plus));
        for (size_t index : { minus, plus }) {
            lv_obj_set_width(s_controls[index], TOKEN_FIRMWARE_CONTROL_MD);
            lv_obj_set_style_text_align(s_controls[index], LV_TEXT_ALIGN_CENTER, 0);
        }
        return value;
    }
    void percent(lv_obj_t* label, uint8_t value) {
        char text[8];
        std::snprintf(text, sizeof(text), "%u%%", value);
        lv_label_set_text(label, text);
    }
    // Shows the settings: toggles checked, values, and unavailable controls disabled.
    void render(const Settings& settings) {
        for (size_t i = 0; i < kChanges.size(); ++i)
            lv_obj_set_state(s_controls[i], LV_STATE_DISABLED, !available(settings, kChanges[i]));
        lv_obj_set_state(s_controls[0], LV_STATE_CHECKED, settings.auto_brightness);
        lv_obj_set_state(s_controls[5], LV_STATE_CHECKED, settings.muted);
        lv_obj_set_state(s_controls[6], LV_STATE_CHECKED, settings.leds_on);
        percent(s_brightness_value, settings.brightness);
        percent(s_volume_value, settings.volume);
        s_shown = settings;
        s_rendered = true;
    }
    bool create(lv_obj_t* container, void*) {
        auto* card = components::card(container);
        lv_obj_set_width(card, lv_pct(85));
        lv_obj_set_height(card, lv_pct(100));
        // Scrolls if the rows outgrow the screen; focus scrolls its control into view.
        lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        auto* header = row(card);
        components::text(header, "Settings", styles::Text::title);
        // Focus order follows creation, so the rows come before Back.
        s_controls[0] = components::checkbox(card, "Automatic brightness", action(0));
        s_brightness_value = stepper(card, "Brightness", 1, 2);
        s_volume_value = stepper(card, "Volume", 3, 4);
        s_controls[5] = components::checkbox(card, "Mute sounds", action(5));
        s_controls[6] = components::checkbox(card, "LED lights", action(6));
        // Touch's way back; a long press of the encoder does the same.
        auto* back = components::button(header, "Back", styles::Control::secondary, { .callback = go_back });
        lv_obj_set_width(back, LV_SIZE_CONTENT);
        for (lv_obj_t* control : s_controls)
            if (!control)
                return false;
        render(current());
        return back && s_brightness_value && s_volume_value;
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
