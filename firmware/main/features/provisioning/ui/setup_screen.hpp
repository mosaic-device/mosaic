#pragma once

#include <string_view>
#include "lvgl.h"

namespace mosaic::features::provisioning {

// LVGL task only. The setup view owns LVGL's displayed credential copies.
struct SetupScreen {
    lv_obj_t* panel = nullptr;
    lv_obj_t* ssid = nullptr;
    lv_obj_t* password = nullptr;
    lv_obj_t* url = nullptr;
    lv_obj_t* qr_frame = nullptr;
    lv_obj_t* qr = nullptr;
    lv_obj_t* qr_caption = nullptr;
    lv_obj_t* failure_label = nullptr;

    void create(lv_obj_t* parent);
    void hide();
    void render_failure(std::string_view failure);
    void show(std::string_view ssid_value, std::string_view password_value, std::string_view url_value,
        std::string_view qr_payload);
};

} // namespace mosaic::features::provisioning
