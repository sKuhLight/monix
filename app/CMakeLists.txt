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

add_executable(monix-gui main.cpp theme.cpp tray.cpp)
target_link_libraries(monix-gui PRIVATE monix imgui glfw ${OPENGL_LIBRARIES})
target_compile_definitions(monix-gui PRIVATE MONIX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
file(COPY ${CMAKE_SOURCE_DIR}/tools/monix-driverctl
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
add_custom_command(TARGET monix-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/tools/monix-driverctl
            $<TARGET_FILE_DIR:monix-gui>/monix-driverctl)

pkg_check_modules(APPINDICATOR ayatana-appindicator3-0.1)
if(APPINDICATOR_FOUND)
    target_compile_definitions(monix-gui PRIVATE MONIX_HAVE_TRAY=1 MONIX_USE_AYATANA_APPINDICATOR=1)
    target_include_directories(monix-gui PRIVATE ${APPINDICATOR_INCLUDE_DIRS})
    target_link_libraries(monix-gui PRIVATE ${APPINDICATOR_LIBRARIES})
    target_link_directories(monix-gui PRIVATE ${APPINDICATOR_LIBRARY_DIRS})
    target_compile_options(monix-gui PRIVATE ${APPINDICATOR_CFLAGS_OTHER})
else()
    pkg_check_modules(APPINDICATOR appindicator3-0.1)
    if(APPINDICATOR_FOUND)
        target_compile_definitions(monix-gui PRIVATE MONIX_HAVE_TRAY=1)
        target_include_directories(monix-gui PRIVATE ${APPINDICATOR_INCLUDE_DIRS})
        target_link_libraries(monix-gui PRIVATE ${APPINDICATOR_LIBRARIES})
        target_link_directories(monix-gui PRIVATE ${APPINDICATOR_LIBRARY_DIRS})
        target_compile_options(monix-gui PRIVATE ${APPINDICATOR_CFLAGS_OTHER})
    endif()
endif()
