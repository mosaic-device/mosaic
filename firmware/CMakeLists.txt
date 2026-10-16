cmake_minimum_required(VERSION 3.21)

# ESP-IDF 5.5.4 / ESP32-P4: keep the application in C++23; vendor components remain C.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_LIST_DIR}/tokens.cmake")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(device_firmware)
