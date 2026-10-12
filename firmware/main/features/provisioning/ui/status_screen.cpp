#include "status_screen.hpp"

#include <cstring>
#include "../../../ui/generated/design_tokens.h"

namespace mosaic::features::provisioning {
namespace {
    lv_color_t color(uint32_t rgb) {
        return lv_color_hex(rgb);
    }
}

bool StatusScreen::create(lv_obj_t* parent) {
    card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(85));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, color(TOKEN_COLOR_SURFACE_DARK), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, TOKEN_RADIUS_LG, 0);
    lv_obj_set_style_pad_all(card, TOKEN_SPACE_8, 0);
    lv_obj_set_style_pad_row(card, TOKEN_SPACE_4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    heading = lv_label_create(card);
    lv_obj_set_width(heading, lv_pct(100));
    lv_label_set_long_mode(heading, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(heading, color(TOKEN_COLOR_FOREGROUND_DARK), 0);
    lv_obj_set_style_text_font(heading, GET_FONT(TOKEN_TEXT_XL), 0);
    lv_label_set_text(heading, "");
    description = lv_label_create(card);
    lv_obj_set_width(description, lv_pct(100));
    lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(description, color(TOKEN_COLOR_MUTED_FOREGROUND_DARK), 0);
    lv_obj_set_style_text_font(description, GET_FONT(TOKEN_TEXT_LG), 0);
    lv_label_set_text(description, "");

    recovery_button = lv_button_create(card);
    lv_obj_set_size(recovery_button, lv_pct(100), 72);
    lv_obj_set_style_bg_color(recovery_button, color(TOKEN_COLOR_PRIMARY_DARK), 0);
    lv_obj_add_event_cb(recovery_button, on_recovery, LV_EVENT_CLICKED, this);
    recovery_label = lv_label_create(recovery_button);
    lv_label_set_text(recovery_label, "");
    lv_obj_set_style_text_color(recovery_label, color(TOKEN_COLOR_PRIMARY_FOREGROUND_DARK), 0);
    lv_obj_set_style_text_font(recovery_label, GET_FONT(TOKEN_TEXT_MD), 0);
    lv_obj_center(recovery_label);
    lv_obj_add_flag(recovery_button, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void StatusScreen::on_recovery(lv_event_t* event) {
    auto* self = static_cast<StatusScreen*>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && self->action_callback)
        self->action_callback(UiAction::SetupTapped, self->action_context);
}
void StatusScreen::hide() {
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
}
void StatusScreen::render(const ViewState& state) {
    lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(heading, state.heading.data());
    lv_label_set_text(description, state.message.data());
    lv_label_set_text(recovery_label, state.recovery_prompt.data());
    if (state.recovery_visible)
        lv_obj_remove_flag(recovery_button, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(recovery_button, LV_OBJ_FLAG_HIDDEN);
    if (state.recovery_enabled)
        lv_obj_remove_state(recovery_button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(recovery_button, LV_STATE_DISABLED);
}
void StatusScreen::bind_action(ActionCallback callback, void* context) {
    action_context = context;
    action_callback = callback;
}
} // namespace mosaic::features::provisioning
