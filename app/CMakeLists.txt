# Monix GUI — Dear ImGui + GLFW + OpenGL3, linked against the monix protocol lib.
include(FetchContent)
FetchContent_Declare(imgui_external
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.4.tar.gz
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(imgui_external)

add_library(imgui STATIC
    ${imgui_external_SOURCE_DIR}/imgui.cpp
    ${imgui_external_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_external_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_external_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_external_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_external_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_external_SOURCE_DIR} ${imgui_external_SOURCE_DIR}/backends)

find_package(OpenGL REQUIRED)

add_executable(monix-gui main.cpp theme.cpp)
target_link_libraries(monix-gui PRIVATE monix imgui glfw ${OPENGL_LIBRARIES})
