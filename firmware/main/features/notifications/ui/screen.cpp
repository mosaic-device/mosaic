#include "screen.hpp"
#include "../flow.hpp"
#include "../store.hpp"
#include "../../clock/service.hpp"
#include "../../led_feedback/patterns.hpp"
#include "../../led_feedback/service.hpp"
#include "../../../ui/components/controls.hpp"
#include "../../../ui/generated/design_tokens.h"

#include <array>
#include <ctime>
#include <string>

namespace mosaic::features::notifications {
namespace {
    namespace components = ui::components;
    namespace styles = ui::styles;
    namespace navigation = ui::navigation;

    // The incoming view's title wraps to at most this many lines; the detail shows it all.
    constexpr int kTitleLines = 2;
    constexpr int kTitleWidth = 960;

    Flow s_flow;
    uint32_t s_seen_arrivals = 0;
    // The notification on screen: a copy, so the store can take the next one meanwhile.
    Notification s_shown;
    std::time_t s_aged_at = -1;
    // The screen has come to the front for the notification showing. Until it has, the
    // notification waits, such as behind the pairing confirmation.
    bool s_opened = false;

    struct Incoming {
        lv_obj_t* panel = nullptr;
        lv_obj_t* meta = nullptr;
        lv_obj_t* title = nullptr;
        lv_obj_t* view = nullptr;
    } s_incoming;
    struct Detail {
        lv_obj_t* panel = nullptr;
        lv_obj_t* source = nullptr;
        lv_obj_t* age = nullptr;
        lv_obj_t* body = nullptr;
        lv_obj_t* title = nullptr;
        lv_obj_t* text = nullptr;
        lv_obj_t* back = nullptr;
    } s_detail;

    // The same blue as the ring and strip for a notification.
    lv_color_t notification_color() {
        const auto& c = led_feedback::kNotificationColor;
        return lv_color_make(c.red, c.green, c.blue);
    }
    lv_obj_t* dot(lv_obj_t* parent) {
        auto* object = lv_obj_create(parent);
        lv_obj_remove_style_all(object);
        lv_obj_set_size(object, TOKEN_SPACE_4, TOKEN_SPACE_4);
        lv_obj_set_style_radius(object, TOKEN_RADIUS_FULL, 0);
        lv_obj_set_style_bg_color(object, notification_color(), 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        return object;
    }
    lv_obj_t* row(lv_obj_t* parent, lv_flex_align_t main) {
        auto* object = lv_obj_create(parent);
        lv_obj_remove_style_all(object);
        lv_obj_set_size(object, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(object, main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        return object;
    }
    void show(lv_obj_t* object, bool visible) {
        if (visible)
            lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }

    // "GitHub · 2 min ago", or just the source until the clock is synced.
    std::string age_text() {
        std::array<char, clock::kAgoSize> buffer;
        const std::string_view age = clock::ago_text(static_cast<std::time_t>(s_shown.occurred_at), buffer);
        return std::string(age);
    }
    void render_age() {
        const std::string age = age_text();
        std::string meta(s_shown.source_view());
        if (!age.empty())
            meta += " \xC2\xB7 " + age;
        lv_label_set_text(s_incoming.meta, meta.c_str());
        lv_label_set_text(s_detail.age, age.empty() ? "" : ("\xC2\xB7 " + age).c_str());
    }

    // At most kTitleLines lines, ending in "..." if it's longer.
    void fit_title(lv_obj_t* label) {
        const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        const int32_t line = lv_font_get_line_height(font) + lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
        lv_point_t size;
        lv_text_get_size(&size, lv_label_get_text(label), font, lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
            lv_obj_get_style_text_line_space(label, LV_PART_MAIN), kTitleWidth, LV_TEXT_FLAG_NONE);
        if (size.y > line * kTitleLines) {
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_height(label, line * kTitleLines);
        } else {
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_height(label, LV_SIZE_CONTENT);
        }
    }

    void render() {
        const Flow::View view = s_flow.view();
        show(s_incoming.panel, view == Flow::View::incoming);
        show(s_detail.panel, view == Flow::View::detail);
        lv_label_set_text(s_incoming.title, std::string(s_shown.title_view()).c_str());
        fit_title(s_incoming.title);
        lv_label_set_text(s_detail.source, std::string(s_shown.source_view()).c_str());
        lv_label_set_text(s_detail.title, std::string(s_shown.title_view()).c_str());
        lv_label_set_text(s_detail.text, std::string(s_shown.text_view()).c_str());
        show(s_detail.text, !s_shown.text_view().empty());
        render_age();
        s_aged_at = std::time(nullptr);
        // Each view starts on its first control: View, or Back.
        lv_obj_t* first = view == Flow::View::detail ? s_detail.back : s_incoming.view;
        lv_group_focus_obj(first);
        lv_obj_scroll_to_y(s_detail.body, 0, LV_ANIM_OFF);
    }

    // Leaves the notification and returns to Home.
    void close() {
        s_flow.dismiss();
        navigation::back();
    }
    void on_view(components::Request, void*) {
        if (s_flow.open_detail())
            render();
    }
    void on_close(components::Request, void*) {
        close();
    }

    void create_incoming(lv_obj_t* container) {
        // A centered glance: where it's from, what happened, and the two choices.
        auto* panel = lv_obj_create(container);
        lv_obj_remove_style_all(panel);
        lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(panel, TOKEN_SPACE_6, 0);
        auto* meta_row = row(panel, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(meta_row, TOKEN_SPACE_3, 0);
        dot(meta_row);
        auto* meta = components::text(meta_row, "", styles::Text::body, true);
        lv_obj_set_width(meta, LV_SIZE_CONTENT);
        auto* title = components::text(panel, "", styles::Text::title);
        lv_obj_set_width(title, kTitleWidth);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        auto* buttons = row(panel, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(buttons, TOKEN_FIRMWARE_SPACE_CONTROL, 0);
        lv_obj_set_style_margin_top(buttons, TOKEN_SPACE_6, 0);
        components::allow_focus_overflow(buttons);
        auto* view = components::button(buttons, "View", styles::Control::primary, { .callback = on_view });
        auto* not_now = components::button(buttons, "Not now", styles::Control::secondary, { .callback = on_close });
        for (auto* button : { view, not_now }) {
            lv_obj_set_width(button, LV_SIZE_CONTENT);
            lv_obj_set_style_min_width(button, 240, 0);
            lv_obj_set_style_pad_hor(button, TOKEN_SPACE_10, 0);
            auto* label = lv_obj_get_child(button, 0);
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_obj_center(label);
        }
        s_incoming = { .panel = panel, .meta = meta, .title = title, .view = view };
    }

    void create_detail(lv_obj_t* container) {
        // Like Settings: where it's from in the header, Back on the right, then everything.
        auto* panel = lv_obj_create(container);
        lv_obj_remove_style_all(panel);
        lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        auto* page = components::page(panel);
        // An empty title, hidden below: the source and its age take its place.
        auto* header = components::header(page, "");
        lv_obj_add_flag(lv_obj_get_child(header, 0), LV_OBJ_FLAG_HIDDEN);
        auto* meta = row(header, LV_FLEX_ALIGN_START);
        lv_obj_move_to_index(meta, 0);
        lv_obj_set_style_pad_column(meta, TOKEN_SPACE_3, 0);
        dot(meta);
        auto* source = components::text(meta, "", styles::Text::body);
        lv_obj_set_width(source, LV_SIZE_CONTENT);
        auto* age = components::text(meta, "", styles::Text::body, true);
        lv_obj_set_width(age, LV_SIZE_CONTENT);
        auto* body = components::fill(page);
        lv_obj_set_style_pad_row(body, TOKEN_SPACE_6, 0);
        lv_obj_set_style_pad_top(body, TOKEN_SPACE_4, 0);
        lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
        auto* title = components::text(body, "", styles::Text::heading);
        lv_obj_set_width(title, lv_pct(75));
        auto* text = components::text(body, "", styles::Text::body);
        lv_obj_set_width(text, lv_pct(75));
        auto* back = components::back_button(header, { .callback = on_close });
        s_detail = {
            .panel = panel, .source = source, .age = age, .body = body, .title = title, .text = text, .back = back
        };
    }

    bool create(lv_obj_t* container, void*) {
        // Both views sit inside the screen's container, which has to let Back's focus ring
        // reach into the screen margin, as a page does.
        components::allow_focus_overflow(container);
        create_incoming(container);
        create_detail(container);
        Notification ignored;
        // Only what arrives from now on opens the screen.
        store::latest(ignored, s_seen_arrivals);
        show(s_incoming.panel, false);
        show(s_detail.panel, false);
        return s_incoming.view && s_detail.back;
    }

    // Every UI tick.
    void update(void*) {
        const uint32_t now = lv_tick_get();
        uint32_t arrivals = 0;
        Notification newest;
        const bool any = store::latest(newest, arrivals);
        if (arrivals != s_seen_arrivals && any) {
            s_seen_arrivals = arrivals;
            s_shown = newest;
            s_flow.arrived(now);
            s_opened = navigation::active() == navigation::Screen::notification;
            render();
        }
        const bool in_front = navigation::active() == navigation::Screen::notification;
        if (s_flow.view() != Flow::View::none && !in_front) {
            // Left another way, such as Back or setup taking over; or still waiting to open,
            // which works once Home or Settings shows.
            if (s_opened)
                s_flow.dismiss();
            else
                s_opened = navigation::open(navigation::Screen::notification);
        }
        // An untouched notification closes; one that never got to open just stops waiting.
        if (s_flow.tick(now)) {
            led_feedback::acknowledge();
            if (in_front)
                navigation::back();
        }
        // Keep "2 min ago" current while it's showing.
        if (s_flow.view() != Flow::View::none && std::time(nullptr) != s_aged_at) {
            s_aged_at = std::time(nullptr);
            render_age();
        }
    }
} // namespace

ui::navigation::Page page() {
    return { .create = create, .update = update, .context = nullptr };
}
} // namespace mosaic::features::notifications
