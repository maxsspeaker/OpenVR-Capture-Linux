cmake_minimum_required(VERSION 3.16)

project(linux-openvr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENVR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../deps/openvr")
set(SOURCES
    linux-openvr.cpp
)

find_package(OpenGL REQUIRED)

add_library(linux-openvr MODULE ${SOURCES})

target_include_directories(linux-openvr PRIVATE
    ${OPENVR_DIR}/headers
)

target_link_directories(linux-openvr PRIVATE
    ${OPENVR_DIR}/lib/linux64
)

target_link_libraries(linux-openvr PRIVATE
    libobs
    openvr_api
    OpenGL::GL
)

target_compile_options(linux-openvr PRIVATE
    -O2
    -fPIC
)

set_target_properties(linux-openvr PROPERTIES
    PREFIX ""
)

set_target_properties_obs(linux-openvr PROPERTIES FOLDER plugins)