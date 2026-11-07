add_library(livezoom_core STATIC
    geometry.cpp
    camera.cpp
    smooth_damp.cpp
    easing.cpp
    follow_preset.cpp
    cursor_motion.cpp
    cursor_style.cpp
    drag_pointer.cpp
    text_lines.cpp
    arrow_head.cpp
)

target_include_directories(livezoom_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
