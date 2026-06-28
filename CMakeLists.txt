cmake_minimum_required(VERSION 3.16)
project(monix VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Protocol library (talks to the audient_id kernel module + ALSA for the
# audible "Speaker Playback Volume").
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)
add_library(monix STATIC lib/monix.cpp)
target_include_directories(monix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib ${ALSA_INCLUDE_DIRS})
target_link_libraries(monix PUBLIC ${ALSA_LIBRARIES})

# CLI verifier
add_executable(monixctl tools/monixctl.cpp)
target_link_libraries(monixctl PRIVATE monix)

# GUI app (added once app/ has sources)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt)
    add_subdirectory(app)
endif()
