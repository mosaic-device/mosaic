#pragma once
#include "lvgl.h"
namespace mosaic::ui::styles {
enum class Text { caption, label, body, heading, title };
enum class Control { primary, secondary, list };
// UI context only. Styles live for the process lifetime; never reset while referenced.
void initialize();
void text(lv_obj_t* object, Text role, bool supporting = false);
void card(lv_obj_t* object);
void control(lv_obj_t* object, Control variant);
}
