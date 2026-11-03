#include "setup_screen.hpp"

#include <cstring>

#include "../../../support/secure_erase.hpp"
#include "../../../ui/components/controls.hpp"
#include "../../../ui/generated/design_tokens.h"

namespace mosaic::features::provisioning {
namespace {
    namespace components = ui::components;
    namespace styles = ui::styles;

    constexpr int32_t kQrSize = 320;
    lv_color_t color(uint32_t rgb) {
        return lv_color_hex(rgb);
    }

    lv_obj_t* column(lv_obj_t* parent) {
        auto* object = lv_obj_create(parent);
        lv_obj_remove_style_all(object);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
        return object;
    }
} // namespace

void SetupScreen::create(lv_obj_t* parent) {
    panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    components::page(panel);
    auto* header = components::header(panel, nullptr);
    components::text(header, "Wi-Fi setup", styles::Text::label, true);
    lv_obj_set_width(lv_obj_get_child(header, 1), LV_SIZE_CONTENT);

    auto* body = components::fill(panel);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(body, TOKEN_SPACE_12, 0);

    auto* instructions = column(body);
    lv_obj_set_height(instructions, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(instructions, 1);
    lv_obj_set_style_pad_row(instructions, TOKEN_SPACE_3, 0);
    components::text(instructions, "Connect your phone", styles::Text::heading);
    failure_label = components::text(instructions, "", styles::Text::body);
    lv_obj_set_style_text_color(failure_label, color(TOKEN_COLOR_DANGER_DARK), 0);
    lv_obj_add_flag(failure_label, LV_OBJ_FLAG_HIDDEN);
    components::text(instructions, "Scan the code to join Mosaic's Wi-Fi. Setup opens on its own.", styles::Text::body);
    auto* fallback = components::text(instructions, "If it doesn't, open", styles::Text::body, true);
    lv_obj_set_style_margin_top(fallback, TOKEN_SPACE_3, 0);
    url = components::text(instructions, "", styles::Text::body);
    lv_obj_set_style_text_color(url, color(TOKEN_COLOR_FOCUS_DARK), 0);
    auto* manual = components::text(instructions, "Or join manually", styles::Text::body, true);
    lv_obj_set_style_margin_top(manual, TOKEN_SPACE_3, 0);
    ssid = components::text(instructions, "", styles::Text::body);
    password = components::text(instructions, "", styles::Text::body);

    auto* qr_column = column(body);
    lv_obj_set_size(qr_column, kQrSize + 2 * TOKEN_SPACE_4, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(qr_column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(qr_column, TOKEN_SPACE_4, 0);
    qr_frame = lv_obj_create(qr_column);
    lv_obj_set_size(qr_frame, kQrSize + 2 * TOKEN_SPACE_4, kQrSize + 2 * TOKEN_SPACE_4);
    lv_obj_set_style_bg_color(qr_frame, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr_frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_frame, 0, 0);
    lv_obj_set_style_radius(qr_frame, TOKEN_RADIUS_LG, 0);
    lv_obj_set_style_pad_all(qr_frame, TOKEN_SPACE_4, 0);
    lv_obj_remove_flag(qr_frame, LV_OBJ_FLAG_SCROLLABLE);
    qr_caption = components::text(qr_column, "Scan to join Wi-Fi", styles::Text::label, true);
    lv_obj_set_style_text_align(qr_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}

void SetupScreen::hide() {
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ssid, "");
    lv_label_set_text(password, "");
    lv_label_set_text(url, "");
    lv_label_set_text(failure_label, "");
    lv_obj_add_flag(failure_label, LV_OBJ_FLAG_HIDDEN);
    if (qr) {
        lv_obj_delete(qr);
        qr = nullptr;
    }
}

void SetupScreen::render_failure(std::string_view failure) {
    lv_label_set_text(failure_label, failure.empty() ? "" : failure.data());
    if (failure.empty())
        lv_obj_add_flag(failure_label, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(failure_label, LV_OBJ_FLAG_HIDDEN);
}

void SetupScreen::show(std::string_view ssid_value, std::string_view password_value, std::string_view url_value,
    std::string_view qr_payload) {
    render_failure("");
    char line[216] { };
    std::memcpy(line, "Wi-Fi: ", 7);
    std::memcpy(line + 7, ssid_value.data(), ssid_value.size());
    line[7 + ssid_value.size()] = '\0';
    lv_label_set_text(ssid, line);
    support::secure_erase(line, sizeof(line));
    std::memcpy(line, "Password: ", 10);
    std::memcpy(line + 10, password_value.data(), password_value.size());
    line[10 + password_value.size()] = '\0';
    lv_label_set_text(password, line);
    support::secure_erase(line, sizeof(line));
    std::memcpy(line, url_value.data(), url_value.size());
    line[url_value.size()] = '\0';
    lv_label_set_text(url, line);
    support::secure_erase(line, sizeof(line));

    if (qr) {
        lv_obj_delete(qr);
        qr = nullptr;
    }
    if (!qr_payload.empty()) {
        qr = lv_qrcode_create(qr_frame);
        if (qr) {
            lv_qrcode_set_size(qr, kQrSize);
            lv_qrcode_set_dark_color(qr, lv_color_black());
            lv_qrcode_set_light_color(qr, lv_color_white());
            lv_qrcode_set_quiet_zone(qr, true);
            lv_obj_center(qr);
        }
    }
    const bool qr_ready = qr && lv_qrcode_update(qr, qr_payload.data(), qr_payload.size()) == LV_RESULT_OK;
    if (!qr_ready && qr) {
        lv_obj_delete(qr);
        qr = nullptr;
    }
    lv_obj_set_style_bg_color(qr_frame, qr_ready ? lv_color_white() : color(TOKEN_COLOR_SURFACE_DARK), 0);
    lv_label_set_text(qr_caption, qr_ready ? "Scan to join Wi-Fi" : "QR unavailable. Join manually.");
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(panel);
}
} // namespace mosaic::features::provisioning
