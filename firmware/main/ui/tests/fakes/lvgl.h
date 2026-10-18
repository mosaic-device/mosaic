#pragma once
#include <cstdint>
struct lv_obj_t { };
struct lv_display_t { };
struct lv_event_t { };
struct lv_timer_t { };
constexpr int LV_OPA_COVER = 255;
constexpr int LV_FLEX_FLOW_COLUMN = 0;
constexpr int LV_FLEX_ALIGN_CENTER = 0;
constexpr int LV_EVENT_FLUSH_FINISH = 1;
constexpr int LV_EVENT_REFR_READY = 2;
inline lv_obj_t* lv_obj_create(lv_obj_t*) {
    static lv_obj_t obj;
    return &obj;
}
inline uint32_t lv_color_hex(uint32_t color) {
    return color;
}
inline void lv_obj_set_style_bg_color(lv_obj_t*, uint32_t, int) { }
inline void lv_obj_set_style_bg_opa(lv_obj_t*, int, int) { }
inline void lv_obj_set_style_pad_all(lv_obj_t*, int, int) { }
inline void lv_obj_set_flex_flow(lv_obj_t*, int) { }
inline void lv_obj_set_flex_align(lv_obj_t*, int, int, int) { }
inline void lv_screen_load(lv_obj_t*) { }
lv_timer_t* lv_timer_create(void (*callback)(lv_timer_t*), unsigned, void*);
void lv_display_add_event_cb(lv_display_t*, void (*callback)(lv_event_t*), int event, void*);
struct lv_font_t { };
inline lv_font_t lv_font_montserrat_20;
inline lv_font_t lv_font_montserrat_22;
inline lv_font_t lv_font_montserrat_24;
inline lv_font_t lv_font_montserrat_32;
inline lv_font_t lv_font_montserrat_40;
inline lv_font_t lv_font_montserrat_48;
inline lv_obj_t* lv_display_get_screen_active(lv_display_t*) {
    return lv_obj_create(nullptr);
}
void lv_obj_invalidate(lv_obj_t*);
