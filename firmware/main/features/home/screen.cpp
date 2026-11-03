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
    std::time_t s_checked = -1; // The last second the clock text was checked.
    std::string s_shown_time;
    std::string s_shown_date;
    bool s_shown_trouble = false;

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
    // Shown while the server is out of reach; the ring is amber for the same reason.
    lv_obj_t* trouble_line(lv_obj_t* parent) {
        auto* row = lv_obj_create(parent);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, TOKEN_SPACE_3, 0);
        auto* dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, TOKEN_SPACE_4, TOKEN_SPACE_4);
        lv_obj_set_style_radius(dot, TOKEN_RADIUS_FULL, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(TOKEN_COLOR_WARNING_DARK), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        auto* label = components::text(row, "Can't reach the server. Retrying.", styles::Text::body, true);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
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
        return s_time && s_date && s_trouble && settings;
    }
    // Every UI tick; checks the clock once a second and redraws only what changed.
    void update(void*) {
        const bool trouble = events::status() == events::Status::error;
        if (trouble != s_shown_trouble) {
            s_shown_trouble = trouble;
            if (trouble)
                lv_obj_remove_flag(s_trouble, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(s_trouble, LV_OBJ_FLAG_HIDDEN);
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
