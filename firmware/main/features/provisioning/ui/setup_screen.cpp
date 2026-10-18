#include "setup_screen.hpp"

#include <cstring>

#include "../../../support/secure_erase.hpp"
#include "../../../ui/generated/design_tokens.h"

namespace mosaic::features::provisioning {
namespace {
    constexpr int32_t kQrSize = 320;
    lv_color_t color(uint32_t rgb) {
        return lv_color_hex(rgb);
    }

    lv_obj_t* plain_panel(lv_obj_t* parent) {
        auto* panel = lv_obj_create(parent);
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_shadow_width(panel, 0, 0);
        lv_obj_set_style_pad_all(panel, 0, 0);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        return panel;
    }

    lv_obj_t* text(lv_obj_t* parent, const char* value, uint32_t rgb) {
        auto* label = lv_label_create(parent);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(label, color(rgb), 0);
        lv_obj_set_style_text_font(label, GET_FONT(TOKEN_TEXT_LG), 0);
        lv_label_set_text(label, value);
        return label;
    }

} // namespace

void SetupScreen::create(lv_obj_t* parent) {
    panel = plain_panel(parent);
    lv_obj_set_size(panel, lv_pct(94), lv_pct(90));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(panel, TOKEN_SPACE_8, 0);

    auto* instructions = plain_panel(panel);
    lv_obj_set_width(instructions, lv_pct(60));
    lv_obj_set_height(instructions, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(instructions, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(instructions, TOKEN_SPACE_4, 0);

    auto* eyebrow = text(instructions, "MOSAIC  /  WI-FI SETUP", TOKEN_COLOR_PRIMARY_DARK);
    lv_obj_set_style_text_font(eyebrow, GET_FONT(TOKEN_TEXT_SM), 0);
    auto* title = text(instructions, "Connect your phone to Mosaic", TOKEN_COLOR_FOREGROUND_DARK);
    lv_obj_set_style_text_font(title, GET_FONT(TOKEN_TEXT_XL), 0);
    failure_label = text(instructions, "", TOKEN_COLOR_PRIMARY_DARK);
    lv_obj_add_flag(failure_label, LV_OBJ_FLAG_HIDDEN);
    text(instructions, "Scan to join Wi-Fi. The setup page should open automatically.", TOKEN_COLOR_FOREGROUND_DARK);
    auto* fallback = text(instructions, "If it doesn't, open:", TOKEN_COLOR_MUTED_FOREGROUND_DARK);
    lv_obj_set_style_text_font(fallback, GET_FONT(TOKEN_TEXT_MD), 0);
    url = text(instructions, "", TOKEN_COLOR_PRIMARY_DARK);
    auto* manual = text(instructions, "JOIN MANUALLY", TOKEN_COLOR_MUTED_FOREGROUND_DARK);
    lv_obj_set_style_text_font(manual, GET_FONT(TOKEN_TEXT_SM), 0);
    ssid = text(instructions, "", TOKEN_COLOR_FOREGROUND_DARK);
    lv_obj_set_style_text_font(ssid, GET_FONT(TOKEN_TEXT_MD), 0);
    password = text(instructions, "", TOKEN_COLOR_FOREGROUND_DARK);
    lv_obj_set_style_text_font(password, GET_FONT(TOKEN_TEXT_MD), 0);

    auto* qr_column = plain_panel(panel);
    lv_obj_set_size(qr_column, kQrSize + 2 * TOKEN_SPACE_4, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(qr_column, LV_FLEX_FLOW_COLUMN);
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
    qr_caption = text(qr_column, "Scan to join Wi-Fi", TOKEN_COLOR_MUTED_FOREGROUND_DARK);
    lv_obj_set_style_text_font(qr_caption, GET_FONT(TOKEN_TEXT_MD), 0);
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
