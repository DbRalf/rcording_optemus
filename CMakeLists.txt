cmake_minimum_required(VERSION 3.10)
project(recording_system_optimus)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find GStreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GSTREAMER_RTSP REQUIRED gstreamer-rtsp-1.0)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${GSTREAMER_INCLUDE_DIRS})
include_directories(${GSTREAMER_APP_INCLUDE_DIRS})

# Add include directory
add_library(gs_recording src/gs_recording.cpp)
target_include_directories(gs_recording PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_APP_INCLUDE_DIRS}
    ${GSTREAMER_RTSP_INCLUDE_DIRS}
)
target_link_libraries(gs_recording PUBLIC
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
    ${GSTREAMER_RTSP_LIBRARIES}
)

# Set compiler flags
target_compile_options(gs_recording PRIVATE ${GSTREAMER_CFLAGS_OTHER})

# Executable
add_executable(recorder src/gs_recording.cpp)
target_include_directories(recorder PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_APP_INCLUDE_DIRS}
    ${GSTREAMER_RTSP_INCLUDE_DIRS}
)
target_link_libraries(recorder PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
    ${GSTREAMER_RTSP_LIBRARIES}
)
target_compile_options(recorder PRIVATE ${GSTREAMER_CFLAGS_OTHER})
