cmake_minimum_required(VERSION 3.16)
project(Therenect VERSION 0.9.2 LANGUAGES C CXX)

# Therenect - Linux port (Raspberry Pi OS / Ubuntu, Kinect V1)
# Dependencies (apt): libfreenect-dev libsdl2-dev librtmidi-dev pkg-config cmake git

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(THERENECT_MIDI "MIDI output via RtMidi (optional)" ON)
set(THERENECT_IMGUI_DIR "" CACHE PATH "Local Dear ImGui checkout (empty = download v1.91.9 from GitHub)")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2>=2.0.18)
pkg_check_modules(FREENECT REQUIRED IMPORTED_TARGET libfreenect)
if(THERENECT_MIDI)
  pkg_check_modules(RTMIDI IMPORTED_TARGET rtmidi)
  if(NOT RTMIDI_FOUND)
    message(WARNING "rtmidi not found - building without MIDI (sudo apt install librtmidi-dev)")
  endif()
endif()

# ---- Dear ImGui (replaces ofxControlPanel) ----
if(THERENECT_IMGUI_DIR)
  set(IMGUI_SRC "${THERENECT_IMGUI_DIR}")
else()
  include(FetchContent)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(imgui)   # imgui has no CMakeLists.txt -> only downloaded
  set(IMGUI_SRC "${imgui_SOURCE_DIR}")
endif()

add_library(imgui STATIC
  ${IMGUI_SRC}/imgui.cpp
  ${IMGUI_SRC}/imgui_draw.cpp
  ${IMGUI_SRC}/imgui_tables.cpp
  ${IMGUI_SRC}/imgui_widgets.cpp
  ${IMGUI_SRC}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_SRC} ${IMGUI_SRC}/backends)
target_link_libraries(imgui PUBLIC PkgConfig::SDL2)

# ---- Therenect ----
add_executable(therenect
  src/main.cpp
  src/Therenect.cpp
  src/KinectV1.cpp
  src/MidiOut.cpp)
target_link_libraries(therenect PRIVATE imgui PkgConfig::SDL2 PkgConfig::FREENECT Threads::Threads)
target_compile_options(therenect PRIVATE -Wall -Wextra)
if(RTMIDI_FOUND)
  target_link_libraries(therenect PRIVATE PkgConfig::RTMIDI)
  target_compile_definitions(therenect PRIVATE THERENECT_HAVE_RTMIDI)
endif()

include(GNUInstallDirs)
install(TARGETS therenect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/therenect.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
