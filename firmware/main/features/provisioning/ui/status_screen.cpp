#include "status_screen.hpp"

#include <cstring>
#include "../../../ui/components/controls.hpp"
#include "../../../ui/generated/design_tokens.h"

namespace mosaic::features::provisioning {
namespace {
    namespace components = ui::components;
    namespace styles = ui::styles;
}

bool StatusScreen::create(lv_obj_t* parent) {
    card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    components::page(card);
    // Boot: just the wordmark, in the middle, until there is something to say.
    boot = components::fill(card);
    lv_obj_set_flex_align(boot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    components::wordmark(boot, true);
    // Everything else: the wordmark in the header and the state in the middle, read
    // top to bottom.
    content = components::fill(card);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, TOKEN_FIRMWARE_SPACE_GAP, 0);
    components::header(content, nullptr);
    auto* middle = components::fill(content);
    lv_obj_set_flex_align(middle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(middle, TOKEN_SPACE_4, 0);
    // Room for the button's focus ring, which draws outside it.
    lv_obj_set_style_pad_all(middle, TOKEN_FOCUS_OFFSET + TOKEN_FOCUS_WIDTH, 0);
    heading = components::text(middle, "", styles::Text::title);
    lv_obj_set_width(heading, lv_pct(75));
    description = components::text(middle, "", styles::Text::body, true);
    lv_obj_set_width(description, lv_pct(75));
    recovery_button = components::button(middle, "", styles::Control::primary);
    lv_obj_set_width(recovery_button, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(recovery_button, 420, 0);
    lv_obj_set_style_pad_hor(recovery_button, TOKEN_SPACE_10, 0);
    lv_obj_set_style_margin_top(recovery_button, TOKEN_SPACE_4, 0);
    recovery_label = lv_obj_get_child(recovery_button, 0);
    lv_obj_set_width(recovery_label, LV_SIZE_CONTENT);
    lv_obj_center(recovery_label);
    lv_obj_add_event_cb(recovery_button, on_recovery, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(recovery_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);
    return heading && description && recovery_label;
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
    if (state.starting) {
        lv_obj_remove_flag(boot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(boot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(content, LV_OBJ_FLAG_HIDDEN);
    }
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
