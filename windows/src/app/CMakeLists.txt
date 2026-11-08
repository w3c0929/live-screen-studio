add_executable(livezoom_win WIN32
    main.cpp
    zoom_controller.cpp
    settings.cpp
    tray.cpp
    annotation.cpp
    overlay_renderer.cpp
    ocr_service.cpp
    settings_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform/win/magnification_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform/win/input_hook.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../assets/livezoom.rc
)

target_include_directories(livezoom_win PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../platform/win
    ${CMAKE_CURRENT_SOURCE_DIR}/../../assets
)

target_link_libraries(livezoom_win PRIVATE
    livezoom_core
    user32
    gdi32
    dwmapi
    d3d11
    dxgi
    d2d1
    dcomp
    shell32
    ole32
    advapi32
)

if(MINGW)
    # One self-contained EXE for the portable release.
    target_link_options(livezoom_win PRIVATE -static)
endif()
