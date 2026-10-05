idf_component_register(
    SRCS "app_main.cpp"
    REQUIRES esp32_p4_wifi6_touch_lcd_5 esp_lcd_hx8394 device_protos nanopb
)

# IDF supplies the cross-compiler; fail instead of silently building owned code as C++17.
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_23)
