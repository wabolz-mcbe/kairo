cmake_minimum_required(VERSION 3.10)
project(kairo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add main source
set(SOURCES
    src/main.cpp
)

# Dear ImGui sources
set(IMGUI_DIR imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_android.cpp
)

# Create shared library
add_library(kairo SHARED ${SOURCES} ${IMGUI_SOURCES})

# Include directories
target_include_directories(kairo PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link libraries
target_link_libraries(kairo
    GLESv2
    EGL
    pthread
)

# Set output directory
set_target_properties(kairo PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    PREFIX "lib"
    SUFFIX ".so"
)
