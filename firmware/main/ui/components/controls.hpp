#pragma once
#include "../styles/styles.hpp"
#include "action.hpp"
#include <string_view>
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

// Screen layout, shared so every screen has the same margins, header and type scale.
// Makes container a page: full size, screen margins, content stacked from the top.
// Returns container.
lv_obj_t* page(lv_obj_t* container);
// A page's top row, a control's height tall, with the title (or the wordmark when title
// is null) on the left. Put actions such as Back in it after creating the page's other
// controls: focus follows creation order, and a screen should start on its content.
lv_obj_t* header(lv_obj_t* page, const char* title);
// Back, for a header: the touch way back (holding the encoder does the same).
lv_obj_t* back_button(lv_obj_t* header, Action back);
// The Mosaic wordmark, tinted to the foreground color.
lv_obj_t* wordmark(lv_obj_t* parent, bool large = false);
// Fills the rest of a page's height, for content between the header and the bottom.
lv_obj_t* fill(lv_obj_t* page);

// A full-width row with a label on the left and a switch on the right. The row is the
// control: focus and taps go to it, and it requests Request::toggle. Show the value
// with render_switch().
lv_obj_t* switch_row(lv_obj_t* parent, const char* label, Action action);
void render_switch(lv_obj_t* row, bool on);

// A labelled level: a slider from 0 to steps with its value beside it. Press to adjust
// with the encoder, turn, then press again; touch drags. Changes report the step.
using OnLevel = void (*)(int step, void* context);
struct Level {
    lv_obj_t* slider = nullptr;
    lv_obj_t* value = nullptr;
    // Under the label: why the level is unavailable, hidden while it is available.
    lv_obj_t* reason = nullptr;
};
Level level_row(lv_obj_t* parent, const char* label, int steps, OnLevel on_level, void* context);
// Shows the step and its text. A non-empty reason disables the slider, so turning
// skips it and touch does nothing, and shows the reason under the label.
void render_level(const Level& level, int step, const char* value, std::string_view reason);
}
