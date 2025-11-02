cmake_minimum_required(VERSION 3.25)
project(lan_jam_synth LANGUAGES CXX)

add_definitions(-D_WIN32_WINNT=0x0A00) # target Windows 10

# vcpkg toolchain hint for local dev
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Asio standalone uses header-only
find_path(ASIO_INCLUDE_DIRS "asio.hpp")
if(NOT ASIO_INCLUDE_DIRS)
  # vcpkg installs it at include/
  set(ASIO_INCLUDE_DIRS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
endif()

find_path(RTAUDIO_INCLUDE_DIR rtaudio/RtAudio.h)
find_library(RTAUDIO_LIBRARY NAMES rtaudio)

find_package(glad CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

include_directories(${ASIO_INCLUDE_DIRS})
include_directories(${RTAUDIO_INCLUDE_DIR})

add_library(core
  src/common/UdpSocket.cpp
  src/common/JitterBuffer.cpp
  src/audio/AudioIO.cpp
  src/audio/SynthVoice.cpp
)
target_include_directories(core PUBLIC src)

target_link_libraries(core PRIVATE ${RTAUDIO_LIBRARY})

add_executable(lan_jam_server src/server/main_server.cpp)
target_link_libraries(lan_jam_server PRIVATE core)

add_executable(lan_jam_client src/client/main_client.cpp)
target_link_libraries(lan_jam_client PRIVATE core)

add_executable(lan_jam_client_gui
  src/client/main_client_gui.cpp
  src/gui/GuiApp.cpp
)
target_link_libraries(lan_jam_client_gui PRIVATE
  core
  imgui::imgui
  glad::glad
  glfw
)

if (WIN32)
  target_link_libraries(lan_jam_client_gui PRIVATE opengl32)
endif()
