
option(MOSAIC_UI_GALLERY "Build isolated development-only UI gallery (no provisioning startup)" OFF)
if(MOSAIC_UI_GALLERY)
    list(APPEND gallery_sources
        "ui/styles/styles.cpp"
        "ui/components/controls.cpp"
        "ui/gallery/gallery.cpp")
endif()

option(MOSAIC_DEBUG_TRIGGERS "Build a development-only serial command that publishes device events" OFF)
if(MOSAIC_DEBUG_TRIGGERS)
    if(MOSAIC_UI_GALLERY)
        message(FATAL_ERROR "MOSAIC_DEBUG_TRIGGERS needs the normal app; the gallery does not start the event bus")
    endif()
    list(APPEND debug_sources "features/debug_triggers/console.cpp")
endif()

option(MOSAIC_DISPLAY_DIAGNOSTICS "Trace the display's PPA and frame-buffer waits" OFF)
if(MOSAIC_DISPLAY_DIAGNOSTICS)
    list(APPEND diagnostic_sources "ui/display_diagnostics.cpp")

endif()

option(MOSAIC_ENCODER_TRACE "Log every rotary encoder step and button press" OFF)

include("${CMAKE_CURRENT_LIST_DIR}/ui/generated/assets/sources.cmake")

idf_component_register(
    SRCS "app_main.cpp"
         "ui/runtime.cpp"
         "drivers/display.cpp"
         "drivers/encoder.cpp"
         "drivers/encoder_logic.cpp"
         "features/provisioning/presentation/presenter.cpp"
         "features/provisioning/ui/renderer.cpp"
         "features/provisioning/ui/setup_screen.cpp"
         "features/provisioning/ui/status_screen.cpp"
         "features/provisioning/state_machine.cpp"
         "features/provisioning/service.cpp"
         "features/provisioning/settings_validation.cpp"
         "features/provisioning/adapters/captive_portal.cpp"
         "features/provisioning/adapters/settings_store.cpp"
         "features/provisioning/adapters/wifi.cpp"
         "events/status.cpp"
         "events/bus.cpp"
         ${gallery_sources}
         ${debug_sources}
         ${diagnostic_sources}
         ${MOSAIC_ASSET_SOURCES}
    REQUIRES esp32_p4_wifi6_touch_lcd_5 esp_lcd_hx8394 device_protos nanopb
             esp_wifi esp_wifi_remote esp_hosted esp_netif esp_event esp_http_server
             nvs_flash esp_timer bootloader_support lwip esp_driver_ppa esp_driver_gpio esp_driver_pcnt
             # Unconditional: IDF resolves requirements before options apply. Unused unless MOSAIC_DEBUG_TRIGGERS.
             console
    EMBED_TXTFILES "features/provisioning/assets/setup.html" "features/provisioning/assets/setup.js"
)

if(MOSAIC_UI_GALLERY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MOSAIC_UI_GALLERY=1)
endif()

if(MOSAIC_DEBUG_TRIGGERS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MOSAIC_DEBUG_TRIGGERS=1)
endif()

if(MOSAIC_ENCODER_TRACE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MOSAIC_ENCODER_TRACE=1)
endif()

# IDF supplies the cross-compiler; fail instead of silently building owned code as C++17.
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_23)


if(MOSAIC_DISPLAY_DIAGNOSTICS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MOSAIC_DISPLAY_DIAGNOSTICS=1)
    target_link_options(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=ppa_do_scale_rotate_mirror"
        "-Wl,--wrap=display_bridge_pipeline_wait_free_buf"
    )
endif()
