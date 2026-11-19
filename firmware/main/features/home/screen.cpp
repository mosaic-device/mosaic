#include "screen.hpp"
#include "../clock/service.hpp"
#include "../../events/bus.hpp"
#include "../../ui/components/controls.hpp"
#include "../../ui/generated/design_tokens.h"
#include <array>
#include <ctime>
#include <string>
namespace mosaic::features::home {
namespace {
    namespace components = ui::components;
    namespace styles = ui::styles;

    lv_obj_t* s_time = nullptr;
    lv_obj_t* s_date = nullptr;
    lv_obj_t* s_trouble = nullptr;
    lv_obj_t* s_trouble_text = nullptr;
    lv_obj_t* s_trouble_dot = nullptr;
    std::time_t s_checked = -1; // The last second the clock text was checked.
    std::string s_shown_time;
    std::string s_shown_date;
    events::Status s_shown_status = events::Status::connected;

    void open_settings(components::Request, void*) {
        ui::navigation::open(ui::navigation::Screen::settings);
    }
    void show_text(lv_obj_t* label, std::string& shown, std::string_view text) {
        if (text == shown)
            return;
        shown = text;
        lv_label_set_text(label, shown.c_str());
    }
    void show_time() {
        std::array<char, clock::kTextSize> time;
        std::array<char, clock::kDateSize> date;
        show_text(s_time, s_shown_time, clock::now_text(time));
        show_text(s_date, s_shown_date, clock::today_text(date));
    }
    // What the line under the clock says about the server, or nothing when all is well.
    const char* trouble_text(events::Status status) {
        switch (status) {
        case events::Status::error:
            return "Can't reach the server. Retrying.";
        case events::Status::update_required:
            return "The server needs newer firmware on this device.";
        case events::Status::reconnecting:
            return "Connecting to the server...";
        case events::Status::disconnected:
        case events::Status::pairing:
        case events::Status::connected:
            break;
        }
        return nullptr;
    }
    // Shown while the server is out of reach; the ring is amber for the same reason.
    lv_obj_t* trouble_line(lv_obj_t* parent) {
        auto* row = lv_obj_create(parent);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, TOKEN_SPACE_3, 0);
        s_trouble_dot = lv_obj_create(row);
        lv_obj_remove_style_all(s_trouble_dot);
        lv_obj_set_size(s_trouble_dot, TOKEN_SPACE_4, TOKEN_SPACE_4);
        lv_obj_set_style_radius(s_trouble_dot, TOKEN_RADIUS_FULL, 0);
        lv_obj_set_style_bg_color(s_trouble_dot, lv_color_hex(TOKEN_COLOR_WARNING_DARK), 0);
        lv_obj_set_style_bg_opa(s_trouble_dot, LV_OPA_COVER, 0);
        s_trouble_text = components::text(row, "", styles::Text::body, true);
        lv_obj_set_width(s_trouble_text, LV_SIZE_CONTENT);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        return row;
    }
    bool create(lv_obj_t* container, void*) {
        auto* page = components::page(container);
        auto* header = components::header(page, nullptr);
        auto* middle = components::fill(page);
        lv_obj_set_flex_align(middle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(middle, TOKEN_SPACE_4, 0);
        s_time = components::text(middle, "", styles::Text::title);
        lv_obj_set_style_text_font(s_time, &mosaic_clock_200, 0);
        lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, 0);
        s_date = components::text(middle, "", styles::Text::heading, true);
        lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
        s_trouble = trouble_line(page);
        // The only control, so the encoder starts on it.
        auto* settings = components::icon_button(header, { .callback = open_settings });
        show_time();
        return s_time && s_date && s_trouble && s_trouble_text && s_trouble_dot && settings;
    }
    // Every UI tick; checks the clock once a second and redraws only what changed.
    void update(void*) {
        const events::Status status = events::status();
        if (status != s_shown_status) {
            s_shown_status = status;
            if (const char* text = trouble_text(status)) {
                lv_label_set_text(s_trouble_text, text);
                // Only trouble gets the warning dot, matching the amber ring; reconnecting is quiet.
                if (status == events::Status::reconnecting)
                    lv_obj_add_flag(s_trouble_dot, LV_OBJ_FLAG_HIDDEN);
                else
                    lv_obj_remove_flag(s_trouble_dot, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(s_trouble, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_trouble, LV_OBJ_FLAG_HIDDEN);
            }
        }
        const std::time_t now = std::time(nullptr);
        if (now == s_checked)
            return;
        s_checked = now;
        show_time();
    }
}
ui::navigation::Page page() {
    return { .create = create, .update = update, .context = nullptr };
}
}
