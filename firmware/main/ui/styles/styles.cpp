#include "styles.hpp"
#include <initializer_list>
#include "../generated/design_tokens.h"
namespace mosaic::ui::styles {
namespace {
    lv_style_t surface, base, primary, secondary, pressed_primary, pressed_secondary, focus, selected, disabled, busy;
    bool initialized = false;
    void fill(lv_style_t* style, uint32_t color, uint8_t opacity = LV_OPA_COVER) {
        lv_style_set_bg_color(style, lv_color_hex(color));
        lv_style_set_bg_opa(style, opacity);
    }
}
void initialize() {
    if (initialized)
        return;
    initialized = true;
    for (auto* style : { &surface, &base, &primary, &secondary, &pressed_primary, &pressed_secondary, &focus, &selected,
             &disabled, &busy })
        lv_style_init(style);
    fill(&surface, TOKEN_COLOR_SURFACE_DARK);
    lv_style_set_radius(&surface, TOKEN_RADIUS_LG);
    lv_style_set_border_width(&surface, TOKEN_BORDER_WIDTH_DEFAULT);
    lv_style_set_border_color(&surface, lv_color_hex(TOKEN_COLOR_BORDER_DARK));
    lv_style_set_pad_all(&surface, TOKEN_FIRMWARE_SPACE_CARD);
    lv_style_set_pad_row(&surface, TOKEN_FIRMWARE_SPACE_GAP);
    lv_style_set_min_height(&base, TOKEN_FIRMWARE_CONTROL_MD);
    lv_style_set_radius(&base, TOKEN_RADIUS_MD);
    lv_style_set_pad_all(&base, TOKEN_FIRMWARE_SPACE_CONTROL);
    lv_style_set_border_width(&base, TOKEN_BORDER_WIDTH_DEFAULT);
    lv_style_set_border_color(&base, lv_color_hex(TOKEN_COLOR_INTERACTIVE_BORDER_DARK));
    lv_style_set_text_font(&base, GET_FONT_WEIGHT(TOKEN_TEXT_SM, TOKEN_FONT_WEIGHT_SEMIBOLD));
    fill(&primary, TOKEN_COLOR_PRIMARY_DARK);
    lv_style_set_text_color(&primary, lv_color_hex(TOKEN_COLOR_PRIMARY_FOREGROUND_DARK));
    fill(&secondary, TOKEN_COLOR_SECONDARY_DARK);
    lv_style_set_text_color(&secondary, lv_color_hex(TOKEN_COLOR_SECONDARY_FOREGROUND_DARK));
    fill(&pressed_primary, TOKEN_COLOR_PRIMARY_PRESSED_DARK);
    fill(&pressed_secondary, TOKEN_COLOR_SECONDARY_PRESSED_DARK);
    lv_style_set_outline_width(&focus, TOKEN_FOCUS_WIDTH);
    lv_style_set_outline_pad(&focus, TOKEN_FOCUS_OFFSET);
    lv_style_set_outline_color(&focus, lv_color_hex(TOKEN_COLOR_FOCUS_DARK));
    lv_style_set_outline_opa(&focus, LV_OPA_COVER);
    fill(&selected, TOKEN_COLOR_SELECTED_DARK, TOKEN_COLOR_SELECTED_OPA_DARK);
    lv_style_set_border_color(&selected, lv_color_hex(TOKEN_COLOR_SELECTED_BORDER_DARK));
    lv_style_set_border_opa(&selected, TOKEN_COLOR_SELECTED_BORDER_OPA_DARK);
    lv_style_set_text_color(&selected, lv_color_hex(TOKEN_COLOR_SELECTED_FOREGROUND_DARK));
    fill(&disabled, TOKEN_COLOR_SURFACE_RAISED_DARK);
    lv_style_set_text_color(&disabled, lv_color_hex(TOKEN_COLOR_MUTED_FOREGROUND_DARK));
    lv_style_set_border_color(&busy, lv_color_hex(TOKEN_COLOR_FOCUS_DARK));
    lv_style_set_border_width(&busy, TOKEN_FOCUS_WIDTH);
}
void text(lv_obj_t* object, Text role, bool supporting) {
    const int size = role == Text::caption ? TOKEN_TEXT_XS
        : role == Text::label              ? TOKEN_TEXT_SM
        : role == Text::heading            ? TOKEN_TEXT_LG
        : role == Text::title              ? TOKEN_TEXT_XL
                                           : TOKEN_TEXT_MD;
    const int weight = role == Text::heading || role == Text::title ? TOKEN_FONT_WEIGHT_MEDIUM
        : role == Text::label                                       ? TOKEN_FONT_WEIGHT_SEMIBOLD
                                                                    : TOKEN_FONT_WEIGHT_NORMAL;
    lv_obj_set_style_text_font(object, GET_FONT_WEIGHT(size, weight), 0);
    lv_obj_set_style_text_color(
        object, lv_color_hex(supporting ? TOKEN_COLOR_MUTED_FOREGROUND_DARK : TOKEN_COLOR_FOREGROUND_DARK), 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
}
void card(lv_obj_t* object) {
    initialize();
    lv_obj_remove_style_all(object);
    lv_obj_add_style(object, &surface, 0);
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
}
void control(lv_obj_t* object, Control variant) {
    initialize();
    lv_obj_remove_style_all(object);
    lv_obj_add_style(object, &base, 0);
    lv_obj_add_style(object, variant == Control::primary ? &primary : &secondary, 0);
    lv_obj_add_style(object, variant == Control::primary ? &pressed_primary : &pressed_secondary, LV_STATE_PRESSED);
    if (variant == Control::list)
        lv_obj_add_style(object, &selected, LV_STATE_CHECKED);
    lv_obj_add_style(object, &focus, LV_STATE_FOCUSED);
    lv_obj_add_style(object, &busy, LV_STATE_USER_1);
    lv_obj_add_style(object, &disabled, LV_STATE_DISABLED);
}
}
