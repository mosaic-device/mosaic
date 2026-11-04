cmake_minimum_required(VERSION 3.20)
project(mosaic_preview C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED LVGL_DIR)
  set(LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../managed_components/lvgl__lvgl")
endif()
# CMake source lists need forward slashes even when the caller passes a native Windows path.
string(REPLACE "\\" "/" LVGL_DIR "${LVGL_DIR}")
if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
  message(FATAL_ERROR "LVGL 9.4.0 is missing; see firmware/README.md")
endif()
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/lvgl_sources.txt" LVGL_FILES)
list(TRANSFORM LVGL_FILES PREPEND "${LVGL_DIR}/")
add_library(preview_lvgl STATIC ${LVGL_FILES})
target_compile_definitions(preview_lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_include_directories(preview_lvgl PUBLIC "${LVGL_DIR}" "${CMAKE_CURRENT_LIST_DIR}")

set(MAIN "${CMAKE_CURRENT_LIST_DIR}/../main")
file(GLOB ASSETS "${MAIN}/ui/generated/assets/*.c")
add_library(preview_assets STATIC ${ASSETS})
target_link_libraries(preview_assets PRIVATE preview_lvgl)
target_include_directories(preview_assets PRIVATE "${MAIN}/ui/generated")

# The host stand-ins for ESP-IDF and FreeRTOS. Firmware sources include these headers
# exactly as they include IDF's.
add_library(preview_platform STATIC
  platform/src/system.cpp
  platform/src/freertos.cpp
  platform/src/esp_event.cpp
  platform/src/nvs.cpp)
target_include_directories(preview_platform PUBLIC platform/include)
target_link_libraries(preview_platform PUBLIC Threads::Threads)

# The cue WAVs, built in as the firmware embeds them.
set(SOUNDS_CPP "${CMAKE_CURRENT_BINARY_DIR}/sounds.cpp")
set(SOUNDS_TEXT "#include \"features/audio_cues/sounds.hpp\"\nnamespace {\n")
foreach(cue notification connected)
  set(wav "${MAIN}/features/audio_cues/generated/${cue}.wav")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${wav}")
  file(READ "${wav}" bytes HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
  string(APPEND SOUNDS_TEXT "const uint8_t k_${cue}[] = {${bytes}};\n")
endforeach()
string(APPEND SOUNDS_TEXT "}\nnamespace mosaic::features::audio_cues {\n"
  "std::span<const uint8_t> sound_file(Cue cue) {\n"
  "    return cue == Cue::connected ? std::span<const uint8_t>(k_connected) : std::span<const uint8_t>(k_notification);\n"
  "}\n}\n")
file(CONFIGURE OUTPUT "${SOUNDS_CPP}" CONTENT "${SOUNDS_TEXT}" @ONLY)

# The setup page and script, as the firmware embeds them, ending in a terminator.
set(SETUP_ASSETS_CPP "${CMAKE_CURRENT_BINARY_DIR}/setup_assets.cpp")
set(SETUP_ASSETS_TEXT "")
foreach(asset html js)
  set(source "${MAIN}/features/provisioning/assets/setup.${asset}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
  file(READ "${source}" bytes HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
  string(TOUPPER "${asset}" name)
  if(name STREQUAL "HTML")
    set(name "Html")
  else()
    set(name "Js")
  endif()
  string(APPEND SETUP_ASSETS_TEXT "extern const unsigned char kSetup${name}[] = {${bytes}0};\n")
endforeach()
file(CONFIGURE OUTPUT "${SETUP_ASSETS_CPP}" CONTENT "${SETUP_ASSETS_TEXT}" @ONLY)

# Everything the device runs, except the adapters that talk to its hardware and radio;
# adapters/ replaces those.
add_executable(mosaic-preview
  main.cpp
  device_view.cpp
  text.cpp
  adapters/bh1750.cpp
  adapters/captive_portal.cpp
  adapters/clock_sync.cpp
  adapters/console.cpp
  adapters/display.cpp
  adapters/encoder.cpp
  adapters/leds.cpp
  adapters/speaker.cpp
  adapters/wifi.cpp
  "${SOUNDS_CPP}"
  "${SETUP_ASSETS_CPP}"
  "${MAIN}/app_main.cpp"
  "${MAIN}/app/screens.cpp"
  "${MAIN}/ui/runtime.cpp"
  "${MAIN}/ui/navigation/host.cpp"
  "${MAIN}/ui/navigation/navigator.cpp"
  "${MAIN}/ui/styles/styles.cpp"
  "${MAIN}/ui/components/controls.cpp"
  "${MAIN}/drivers/encoder_logic.cpp"
  "${MAIN}/events/status.cpp"
  "${MAIN}/events/bus.cpp"
  "${MAIN}/features/audio_cues/cues.cpp"
  "${MAIN}/features/audio_cues/playback.cpp"
  "${MAIN}/features/audio_cues/service.cpp"
  "${MAIN}/features/audio_cues/wav.cpp"
  "${MAIN}/features/brightness/policy.cpp"
  "${MAIN}/features/brightness/service.cpp"
  "${MAIN}/features/clock/clock.cpp"
  "${MAIN}/features/clock/service.cpp"
  "${MAIN}/features/debug_triggers/commands.cpp"
  "${MAIN}/features/home/screen.cpp"
  "${MAIN}/features/led_feedback/patterns.cpp"
  "${MAIN}/features/led_feedback/service.cpp"
  "${MAIN}/features/provisioning/presentation/presenter.cpp"
  "${MAIN}/features/provisioning/ui/renderer.cpp"
  "${MAIN}/features/provisioning/ui/setup_screen.cpp"
  "${MAIN}/features/provisioning/ui/status_screen.cpp"
  "${MAIN}/features/provisioning/state_machine.cpp"
  "${MAIN}/features/provisioning/wifi_observation.cpp"
  "${MAIN}/features/provisioning/portal_replies.cpp"
  "${MAIN}/features/provisioning/service.cpp"
  "${MAIN}/features/provisioning/settings_validation.cpp"
  "${MAIN}/features/provisioning/adapters/settings_store.cpp"
  "${MAIN}/features/settings/screen.cpp"
  "${MAIN}/features/settings/service.cpp"
  "${MAIN}/features/settings/settings.cpp"
  "${MAIN}/features/settings/store.cpp")
target_include_directories(mosaic-preview PRIVATE "${MAIN}" "${MAIN}/ui/generated" "${CMAKE_CURRENT_LIST_DIR}"
  ${SDL2_INCLUDE_DIRS})
# The preview always has the development console: it is how the simulated world is driven.
target_compile_definitions(mosaic-preview PRIVATE MOSAIC_DEBUG_TRIGGERS=1)
if(MSVC)
  # Standard __VA_OPT__ for the logging macros, UTF-8 sources, and the POSIX time calls.
  target_compile_options(preview_platform PUBLIC /Zc:preprocessor /utf-8)
  target_compile_options(mosaic-preview PRIVATE
    "/FI${CMAKE_CURRENT_LIST_DIR}/platform/include/mosaic_posix_compat.h")
endif()
if(TARGET SDL2::SDL2)
  set(PREVIEW_SDL SDL2::SDL2)
else()
  set(PREVIEW_SDL ${SDL2_LIBRARIES})
endif()
target_link_libraries(mosaic-preview PRIVATE preview_platform preview_assets preview_lvgl ${PREVIEW_SDL})
if(WIN32)
  # Winsock's min/max macros collide with std::min and std::max in preview sources.
  target_compile_definitions(mosaic-preview PRIVATE NOMINMAX)
  target_link_libraries(mosaic-preview PRIVATE ws2_32)
endif()
if(APPLE)
  find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
  target_link_libraries(mosaic-preview PRIVATE ${COREGRAPHICS_FRAMEWORK})
endif()
