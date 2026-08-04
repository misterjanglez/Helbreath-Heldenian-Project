cmake_minimum_required(VERSION 3.16)

project(CControls LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure-time guard against a source file on disk that no list compiles
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/source_guard.cmake)

set(CCONTROLS_SOURCES
    control.cpp
    control_collection.cpp
    panel.cpp
    button.cpp
    text_input.cpp
    textbox.cpp
    label.cpp
    toggle_button.cpp
    focus_manager.cpp
)

hb_guard_source_list(
    LIST  CCONTROLS_SOURCES
    DIR   ${CMAKE_CURRENT_SOURCE_DIR}
    LABEL "CControls sources"
)

add_library(CControls STATIC ${CCONTROLS_SOURCES})

target_include_directories(CControls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
