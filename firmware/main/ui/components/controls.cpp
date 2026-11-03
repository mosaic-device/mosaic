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
    // Lets children's focus rings draw outside this object. LVGL clips children of an
    // overflowing object to its extended draw area, which a plain layout row lacks.
    void ring_room(lv_event_t* e) {
        lv_event_set_ext_draw_size(e, TOKEN_FOCUS_OFFSET + TOKEN_FOCUS_WIDTH);
    }
    void allow_focus_overflow(lv_obj_t* object) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_add_event_cb(object, ring_room, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
        lv_obj_refresh_ext_draw_size(object);
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
lv_obj_t* page(lv_obj_t* container) {
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    // Children size themselves; header(), fill() and rows are full width.
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(container, TOKEN_FIRMWARE_SPACE_GAP, 0);
    // Controls at the edge, such as Back, draw their focus ring into the screen margin.
    allow_focus_overflow(container);
    return container;
}
lv_obj_t* wordmark(lv_obj_t* parent, bool large) {
    return image(parent, large ? &mosaic_wordmark_60 : &mosaic_wordmark_40);
}
lv_obj_t* header(lv_obj_t* page, const char* title) {
    auto* row = lv_obj_create(page);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), TOKEN_FIRMWARE_CONTROL_MD);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    // Focus rings draw outside their control; the header's edge must not cut them off.
    allow_focus_overflow(row);
    if (title) {
        auto* label = text(row, title, styles::Text::heading);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
    } else {
        wordmark(row);
    }
    return row;
}
lv_obj_t* back_button(lv_obj_t* header, Action back) {
    auto* object = button(header, "Back", styles::Control::secondary, back);
    lv_obj_set_width(object, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(object, TOKEN_SPACE_8, 0);
    // A full-width label would collapse inside a content-width button.
    lv_obj_set_width(lv_obj_get_child(object, 0), LV_SIZE_CONTENT);
    return object;
}
lv_obj_t* fill(lv_obj_t* page) {
    auto* object = lv_obj_create(page);
    lv_obj_remove_style_all(object);
    lv_obj_set_width(object, lv_pct(100));
    lv_obj_set_flex_grow(object, 1);
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    allow_focus_overflow(object);
    return object;
}
lv_obj_t* switch_row(lv_obj_t* parent, const char* label, Action action) {
    auto* row = lv_button_create(parent);
    styles::control(row, styles::Control::secondary);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, TOKEN_FIRMWARE_SPACE_CARD, 0);
    auto* name = text(row, label);
    lv_obj_set_width(name, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(name, 1);
    auto* indicator = lv_switch_create(row);
    styles::toggle(indicator);
    lv_obj_set_size(indicator, 96, 52);
    // Only the row takes focus and taps; the switch just shows the value.
    lv_group_remove_obj(indicator);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CHECKABLE);
    bind(row, action, Request::toggle);
    return row;
}
void render_switch(lv_obj_t* row, bool on) {
    lv_obj_set_state(row, LV_STATE_CHECKED, on);
    if (auto* indicator = lv_obj_get_child(row, 1))
        lv_obj_set_state(indicator, LV_STATE_CHECKED, on);
}
namespace {
    struct LevelBinding {
        OnLevel on_level;
        void* context;
    };
    void level_event(lv_event_t* e) {
        auto* binding = static_cast<LevelBinding*>(lv_event_get_user_data(e));
        if (lv_event_get_code(e) == LV_EVENT_DELETE) {
            delete binding;
            return;
        }
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED && !lv_obj_has_state(slider, LV_STATE_DISABLED))
            binding->on_level(lv_slider_get_value(slider), binding->context);
    }
}
Level level_row(lv_obj_t* parent, const char* label, int steps, OnLevel on_level, void* context) {
    // Looks like a switch row so the list reads as one; the slider inside is the control.
    auto* row = lv_obj_create(parent);
    styles::control(row, styles::Control::secondary);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_group_remove_obj(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(row, TOKEN_FIRMWARE_SPACE_CARD, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, TOKEN_FIRMWARE_SPACE_GAP, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    allow_focus_overflow(row);
    auto* names = lv_obj_create(row);
    lv_obj_remove_style_all(names);
    lv_obj_set_size(names, lv_pct(32), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(names, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(names, LV_OBJ_FLAG_SCROLLABLE);
    text(names, label);
    Level level;
    level.reason = text(names, "", styles::Text::caption, true);
    lv_obj_add_flag(level.reason, LV_OBJ_FLAG_HIDDEN);
    level.slider = lv_slider_create(row);
    styles::slider(level.slider);
    lv_obj_set_size(level.slider, lv_pct(45), TOKEN_SPACE_3);
    // Room for the knob and focus ring inside the row.
    lv_obj_set_style_margin_hor(level.slider, TOKEN_SPACE_4, 0);
    lv_slider_set_range(level.slider, 0, steps);
    level.value = text(row, "", styles::Text::body);
    lv_obj_set_width(level.value, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(level.value, 1);
    lv_obj_set_style_text_align(level.value, LV_TEXT_ALIGN_RIGHT, 0);
    auto* binding = new (std::nothrow) LevelBinding { on_level, context };
    if (!binding) {
        lv_obj_add_state(level.slider, LV_STATE_DISABLED);
        return level;
    }
    lv_obj_add_event_cb(level.slider, level_event, LV_EVENT_ALL, binding);
    return level;
}
void render_level(const Level& level, int step, const char* value, std::string_view reason) {
    const bool unavailable = !reason.empty();
    if (lv_slider_get_value(level.slider) != step)
        lv_slider_set_value(level.slider, step, LV_ANIM_OFF);
    lv_obj_set_state(level.slider, LV_STATE_DISABLED, unavailable);
    lv_label_set_text(level.value, value);
    lv_obj_set_style_text_color(
        level.value, lv_color_hex(unavailable ? TOKEN_COLOR_MUTED_FOREGROUND_DARK : TOKEN_COLOR_FOREGROUND_DARK), 0);
    if (unavailable) {
        // Reasons are short literals; copy into the label anyway, since a view may not end in NUL.
        char text[48] { };
        reason.copy(text, sizeof(text) - 1);
        lv_label_set_text(level.reason, text);
        lv_obj_remove_flag(level.reason, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(level.reason, LV_OBJ_FLAG_HIDDEN);
    }
}
}
