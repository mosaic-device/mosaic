cmake_minimum_required(VERSION 3.20)
project(mosaic_preview C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)

if(NOT DEFINED LVGL_DIR)
  set(LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../managed_components/lvgl__lvgl")
endif()
if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
  message(FATAL_ERROR "LVGL 9.4.0 is missing; see firmware/README.md")
endif()
find_package(SDL2 REQUIRED)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/lvgl_sources.txt" LVGL_FILES)
list(TRANSFORM LVGL_FILES PREPEND "${LVGL_DIR}/")
add_library(preview_lvgl STATIC ${LVGL_FILES})
target_compile_definitions(preview_lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_include_directories(preview_lvgl PUBLIC "${LVGL_DIR}" "${CMAKE_CURRENT_LIST_DIR}")

file(GLOB ASSETS "${CMAKE_CURRENT_LIST_DIR}/../main/ui/generated/assets/*.c")
add_library(preview_assets STATIC ${ASSETS})
target_link_libraries(preview_assets PRIVATE preview_lvgl)
target_include_directories(preview_assets PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../main/ui/generated")

set(MAIN "${CMAKE_CURRENT_LIST_DIR}/../main")
add_executable(mosaic-preview
  main.cpp
  "${MAIN}/ui/styles/styles.cpp"
  "${MAIN}/ui/components/controls.cpp"
  "${MAIN}/ui/navigation/host.cpp"
  "${MAIN}/ui/navigation/navigator.cpp"
  "${MAIN}/features/home/screen.cpp"
  "${MAIN}/features/settings/screen.cpp"
  "${MAIN}/features/settings/settings.cpp"
  "${MAIN}/features/clock/clock.cpp"
  "${MAIN}/features/provisioning/ui/status_screen.cpp"
  "${MAIN}/features/provisioning/ui/setup_screen.cpp"
  "${MAIN}/features/provisioning/presentation/presenter.cpp"
  "${MAIN}/features/provisioning/state_machine.cpp"
  "${MAIN}/events/status.cpp")
target_include_directories(mosaic-preview PRIVATE "${MAIN}" "${MAIN}/ui/generated" fakes ${SDL2_INCLUDE_DIRS})
target_link_libraries(mosaic-preview PRIVATE preview_assets preview_lvgl ${SDL2_LIBRARIES})
