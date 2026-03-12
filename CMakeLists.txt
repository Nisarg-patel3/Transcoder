cmake_minimum_required(VERSION 3.16)
project(transcoder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXE_LINKER_FLAGS "-Wl,--disable-new-dtags")

# ── Qt (try Qt6 first, fall back to Qt5) ──────────────────────────────────────
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(QT Qt6)
    message(STATUS "Using Qt6")
else()
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    set(QT Qt5)
    message(STATUS "Using Qt5")
endif()

# Required for Q_OBJECT / moc
set(CMAKE_AUTOMOC ON)
# set_property(SOURCE "${CMAKE_SOURCE_DIR}/examples/gui.cpp" PROPERTY SKIP_AUTOMOC ON)

# -------------------------------
# Local FFmpeg path
# -------------------------------
set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/deps/external/ffmpeg/linux")

include_directories(
    ${FFMPEG_ROOT}/include
)

link_directories(
    ${FFMPEG_ROOT}/lib
)

list(APPEND CMAKE_BUILD_RPATH "${FFMPEG_ROOT}/lib")

# -------------------------------
# Threads
# -------------------------------
find_package(Threads REQUIRED)

# -------------------------------
# Sources
# -------------------------------
file(GLOB_RECURSE SRC_FILES src/*.cpp)

add_library(transcoder STATIC ${SRC_FILES})

target_include_directories(transcoder PUBLIC include/public PRIVATE include/private)

target_link_libraries(transcoder PUBLIC
    avformat
    avcodec
    avutil
    swscale
    swresample
    avdevice
    avfilter
    Threads::Threads
)

#-------------------------------------
# Examples
#-------------------------------------
add_executable(example_1 examples/example_1.cpp)
target_link_libraries(example_1 PRIVATE transcoder)
add_executable(example_2 examples/example_2.cpp)
target_link_libraries(example_2 PRIVATE transcoder)
add_executable(cli examples/cli.cpp)
target_link_libraries(cli PRIVATE transcoder)
add_executable(gui examples/gui_main.cpp)
target_link_libraries(gui PRIVATE 
    transcoder
    ${QT}::Widgets
)