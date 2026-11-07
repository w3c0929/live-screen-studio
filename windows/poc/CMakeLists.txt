function(add_poc name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE
        user32
        gdi32
        dwmapi
        d3d11
        dxgi
        d2d1
        dcomp
        shell32
        ole32
        shcore
        windowscodecs
    )
    target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
endfunction()

add_poc(mag_poc)
add_poc(input_poc)
add_poc(overlay_poc)
