#include "controls.hpp"
#include "../generated/design_tokens.h"
#include <new>
namespace mosaic::ui::components {
namespace {
    struct Binding {
        Action action;
        Request request;
    };
    void event(lv_event_t* e) {
        auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
        if (lv_event_get_code(e) == LV_EVENT_DELETE) {
            delete binding;
            return;
        }
        auto* object = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (lv_event_get_code(e) == LV_EVENT_CLICKED)
            request({ .disabled = lv_obj_has_state(object, LV_STATE_DISABLED),
                        .busy = lv_obj_has_state(object, LV_STATE_USER_1) },
                binding->action, binding->request);
    }
    void bind(lv_obj_t* object, Action action, Request request) {
        auto* binding = new (std::nothrow) Binding { action, request };
        if (!binding) {
            lv_obj_add_state(object, LV_STATE_DISABLED);
            return;
        }
        lv_obj_add_event_cb(object, event, LV_EVENT_ALL, binding);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    }
}
lv_obj_t* text(lv_obj_t* parent, const char* value, styles::Text role, bool supporting) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    styles::text(label, role, supporting);
    lv_label_set_text(label, value);
    lv_obj_set_width(label, LV_PCT(100));
    return label;
}
lv_obj_t* card(lv_obj_t* parent) {
    auto* object = lv_obj_create(parent);
    styles::card(object);
    lv_obj_set_size(object, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
lv_obj_t* button(lv_obj_t* parent, const char* value, styles::Control variant, Action action) {
    auto* object = lv_button_create(parent);
    styles::control(object, variant);
    lv_obj_set_size(object, LV_PCT(100), LV_SIZE_CONTENT);
    auto* label = lv_label_create(object);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, value);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    bind(object, action, variant == styles::Control::list ? Request::select : Request::activate);
    return object;
}
lv_obj_t* image(lv_obj_t* parent, const lv_image_dsc_t* source) {
    auto* object = lv_image_create(parent);
    lv_image_set_src(object, source);
    lv_obj_set_style_image_recolor(object, lv_color_hex(TOKEN_COLOR_FOREGROUND_DARK), 0);
    lv_obj_set_style_image_recolor_opa(object, LV_OPA_COVER, 0);
    return object;
}
lv_obj_t* icon_button(lv_obj_t* parent, Action action) {
    auto* object = lv_button_create(parent);
    styles::control(object, styles::Control::secondary);
    lv_obj_set_size(object, TOKEN_FIRMWARE_CONTROL_MD, TOKEN_FIRMWARE_CONTROL_MD);
    auto* icon = image(object, &mosaic_icon_settings_40);
    lv_obj_set_style_image_recolor(icon, lv_color_hex(TOKEN_COLOR_SECONDARY_FOREGROUND_DARK), 0);
    lv_obj_center(icon);
    bind(object, action, Request::activate);
    return object;
}
lv_obj_t* checkbox(lv_obj_t* parent, const char* value, Action action) {
    // A passive check row: no LVGL automatic check mutation before owner approval.
    auto* object = lv_checkbox_create(parent);
    styles::control(object, styles::Control::list);
    lv_obj_set_size(object, LV_PCT(100), LV_SIZE_CONTENT);
    lv_checkbox_set_text(object, value);
    lv_obj_set_style_bg_color(object, lv_color_hex(TOKEN_COLOR_FIELD_DARK), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(object, TOKEN_BORDER_WIDTH_DEFAULT, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(object, lv_color_hex(TOKEN_COLOR_INTERACTIVE_BORDER_DARK), LV_PART_INDICATOR);
    lv_obj_set_style_radius(object, TOKEN_RADIUS_SM, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(object, TOKEN_SPACE_2, LV_PART_INDICATOR);
    constexpr lv_style_selector_t checked_indicator
        = static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(object, lv_color_hex(TOKEN_COLOR_PRIMARY_DARK), checked_indicator);
    lv_obj_set_style_text_color(object, lv_color_hex(TOKEN_COLOR_PRIMARY_FOREGROUND_DARK), checked_indicator);
    lv_obj_set_style_text_font(object, LV_FONT_DEFAULT, LV_PART_INDICATOR);
    lv_obj_set_style_min_width(object, TOKEN_FIRMWARE_ICON_MD, LV_PART_INDICATOR);
    lv_obj_set_style_min_height(object, TOKEN_FIRMWARE_ICON_MD, LV_PART_INDICATOR);
    bind(object, action, Request::toggle);
    return object;
}
void render(lv_obj_t* object, State state) {
    lv_obj_remove_state(
        object, static_cast<lv_state_t>(LV_STATE_CHECKED | LV_STATE_FOCUSED | LV_STATE_DISABLED | LV_STATE_USER_1));
    if (state.selected)
        lv_obj_add_state(object, LV_STATE_CHECKED);
    if (state.focused)
        lv_obj_add_state(object, LV_STATE_FOCUSED);
    if (state.disabled || state.busy)
        lv_obj_add_state(object, LV_STATE_DISABLED);
    if (state.busy)
        lv_obj_add_state(object, LV_STATE_USER_1);
    auto* child = lv_obj_get_child(object, 0);
    if (child && lv_obj_check_type(child, &lv_image_class))
        lv_obj_set_style_image_recolor(child, lv_obj_get_style_text_color(object, LV_PART_MAIN), 0);
}
}
