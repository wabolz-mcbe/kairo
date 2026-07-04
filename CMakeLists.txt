cmake_minimum_required(VERSION 3.10)
project(kairo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Some Android NDK toolchain defaults hide exported symbols, which would
# silently break eglSwapBuffers interposition. Force default visibility.
set(CMAKE_CXX_VISIBILITY_PRESET default)
set(CMAKE_VISIBILITY_INLINES_HIDDEN OFF)

# imgui/ is cloned into the repo root by the CI workflow before this runs.
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)

add_library(kairo SHARED
    src/main.cpp
    src/kairo_gui.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(kairo PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_compile_definitions(kairo PRIVATE IMGUI_IMPL_OPENGL_ES2)

target_link_libraries(kairo
    GLESv2
    EGL
    log
    android
    dl
)

set_target_properties(kairo PROPERTIES
    OUTPUT_NAME "kairo"
    PREFIX "lib"
)
