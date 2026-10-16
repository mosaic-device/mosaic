#include "gallery.hpp"
#include "../components/controls.hpp"
#include "../generated/design_tokens.h"
namespace mosaic::ui::gallery {
namespace {
    namespace c = components;
    namespace s = styles;
    struct Preview {
        lv_obj_t* host = nullptr;
        lv_obj_t* toggle = nullptr;
        lv_obj_t* selection = nullptr;
        lv_obj_t* status = nullptr;
        bool checked = false;
        bool selected = false;
        unsigned updates = 0;
        bool rebuild_requested = false;
    } preview;
    void update() {
        c::render(preview.toggle, { .selected = preview.checked });
        lv_checkbox_set_text(preview.toggle, preview.checked ? "Preview option enabled" : "Preview option disabled");
        c::render(preview.selection, { .selected = preview.selected });
        lv_label_set_text_fmt(preview.status, "Owner-applied updates: %u", preview.updates);
    }
    void requested(c::Request request, void*) {
        ++preview.updates;
        if (request == c::Request::toggle)
            preview.checked = !preview.checked;
        if (request == c::Request::select)
            preview.selected = !preview.selected;
        update();
    }
    void populate();
    void rebuild(c::Request, void*) {
        preview.rebuild_requested = true;
    }
    void populate() {
        auto* host = preview.host;
        c::image(host, &mosaic_wordmark_60);
        c::image(host, &mosaic_wordmark_40);
        c::text(host, "Design system · development only", s::Text::title);
        c::text(host,
            "Swipe vertically. No networking, provisioning or stored credentials. Fixed state samples below are not "
            "interactive.",
            s::Text::body, true);
        auto* type = c::card(host);
        c::text(type, "Manrope 500 · Heading", s::Text::heading);
        c::text(type, "Manrope 600 · Label", s::Text::label);
        c::text(type, "Manrope 400 · Body — café, naïve, São Paulo, £ € °C ± × ÷", s::Text::body);
        c::text(type, "Caption · 0123456789 · 08:48 → 18:59", s::Text::caption);
        c::text(type,
            "Long values wrap within their container instead of escaping: A deliberately long device name in the "
            "living room beside the kitchen.");
        auto* live = c::card(host);
        c::text(live, "Interactive · owner applies requests", s::Text::heading);
        c::button(live, "Primary action · white text", s::Control::primary, { requested });
        c::button(live, "Secondary action", s::Control::secondary, { requested });
        c::icon_button(live, { requested });
        c::text(live, "Settings icon button", s::Text::caption, true);
        preview.selection = c::button(live, "Tap to select this long list row; selection is independent of focus",
            s::Control::list, { requested });
        preview.toggle = c::checkbox(live, "", { requested });
        preview.status = c::text(live, "", s::Text::caption);
        update();
        auto* fixed = c::card(host);
        c::text(fixed, "Fixed visual samples · no actions", s::Text::heading);
        auto sample = [fixed](const char* title, s::Control variant, c::State state, bool pressed = false) {
            auto* object = c::button(fixed, title, variant);
            c::render(object, state);
            if (pressed)
                lv_obj_add_state(object, LV_STATE_PRESSED);
            lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        };
        sample("Primary pressed · white text", s::Control::primary, { }, true);
        sample("Secondary pressed", s::Control::secondary, { }, true);
        sample("Focused, not selected", s::Control::list, { .focused = true });
        sample("Selected, not focused", s::Control::list, { .selected = true });
        sample("Selected and focused", s::Control::list, { .selected = true, .focused = true });
        sample("Disabled", s::Control::primary, { .disabled = true });
        sample("Busy · waiting for owner", s::Control::primary, { .busy = true });
        c::button(host, "Recreate gallery controls", s::Control::secondary, { rebuild });
    }
    bool create(lv_obj_t* screen, void*) {
        s::initialize();
        lv_obj_set_style_pad_all(screen, TOKEN_FIRMWARE_SPACE_SCREEN, 0);
        lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        preview.host = lv_obj_create(screen);
        lv_obj_remove_style_all(preview.host);
        lv_obj_set_size(preview.host, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(preview.host, TOKEN_FOCUS_OFFSET + TOKEN_FOCUS_WIDTH, 0);
        lv_obj_set_style_pad_row(preview.host, TOKEN_FIRMWARE_SPACE_GAP, 0);
        lv_obj_set_flex_flow(preview.host, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(preview.host, LV_DIR_VER);
        populate();
        return true;
    }
    void routine(void*) {
        if (!preview.rebuild_requested)
            return;
        preview.rebuild_requested = false;
        lv_obj_clean(preview.host);
        preview.checked = false;
        preview.selected = false;
        populate();
    }
    bool reject_credentials(const void*, size_t, void*) {
        return false;
    }
    void cancel(void*) { }
}
Renderer renderer_callbacks() {
    return { create, routine, reject_credentials, cancel, nullptr };
}
}
