#pragma once
#include "../styles/styles.hpp"
#include "action.hpp"
namespace mosaic::ui::components {

// All calls in the UI context. Returned objects belong to parent. Callbacks/context
// must outlive the object. Requests never update values; the owner calls render.
lv_obj_t* text(lv_obj_t* parent, const char* value, styles::Text role = styles::Text::body, bool supporting = false);
lv_obj_t* card(lv_obj_t* parent);
lv_obj_t* button(lv_obj_t* parent, const char* value, styles::Control variant, Action action = { });
lv_obj_t* icon_button(lv_obj_t* parent, Action action = { });
lv_obj_t* checkbox(lv_obj_t* parent, const char* value, Action action = { });
void render(lv_obj_t* control, State state);
lv_obj_t* image(lv_obj_t* parent, const lv_image_dsc_t* source);
}
