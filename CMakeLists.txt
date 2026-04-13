cmake_minimum_required(VERSION 3.25)
project(lan_jam_synth LANGUAGES CXX)

add_definitions(-D_WIN32_WINNT=0x0A00) # target Windows 10

# vcpkg toolchain hint for local dev
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(LANJAM_ENABLE_CUDA "Build CUDA prototype kernels" OFF)

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
  src/audio/CpuPolyRenderer.cpp
  src/audio/CudaPolyRenderer.cpp
  src/audio/CudaVoiceKernelStub.cpp
)
target_include_directories(core PUBLIC src)

target_link_libraries(core PRIVATE ${RTAUDIO_LIBRARY})

# vcpkg often provides RtAudio (and GLFW) as DLLs; Windows does not search next to the .lib,
# so copy runtime DLLs next to each executable we ship from this project.
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  file(TO_CMAKE_PATH "${VCPKG_INSTALLED_DIR}" LANJAM_VCPKG_INSTALLED)
  set(LANJAM_VCPKG_BIN "${LANJAM_VCPKG_INSTALLED}/${VCPKG_TARGET_TRIPLET}/bin")
  set(LANJAM_VCPKG_DEBUG_BIN "${LANJAM_VCPKG_INSTALLED}/${VCPKG_TARGET_TRIPLET}/debug/bin")
endif()

function(lanjam_copy_dll_from_vcpkg target dll_basename)
  if(NOT WIN32 OR NOT DEFINED LANJAM_VCPKG_BIN)
    return()
  endif()
  set(_dll_dbg "${LANJAM_VCPKG_DEBUG_BIN}/${dll_basename}.dll")
  set(_dll_rel "${LANJAM_VCPKG_BIN}/${dll_basename}.dll")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -D "DLL_DBG=${_dll_dbg}"
      -D "DLL_REL=${_dll_rel}"
      -D "OUT_DIR=$<TARGET_FILE_DIR:${target}>"
      -D "DLL_NAME=${dll_basename}.dll"
      -D "CFG=$<CONFIG>"
      -P "${CMAKE_SOURCE_DIR}/cmake/CopyVcpkgDll.cmake"
    VERBATIM)
endfunction()

add_executable(lan_jam_server src/server/main_server.cpp)
target_link_libraries(lan_jam_server PRIVATE core)
lanjam_copy_dll_from_vcpkg(lan_jam_server rtaudio)

add_executable(lan_jam_client src/client/main_client.cpp)
target_link_libraries(lan_jam_client PRIVATE core)
lanjam_copy_dll_from_vcpkg(lan_jam_client rtaudio)

add_executable(lan_jam_client_gui
  src/client/main_client_gui.cpp
  src/gui/GuiApp.cpp
  src/gui/GuiStyle.cpp
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
lanjam_copy_dll_from_vcpkg(lan_jam_client_gui rtaudio)
lanjam_copy_dll_from_vcpkg(lan_jam_client_gui glfw3)

add_executable(lan_jam_server_gui
  src/server/main_server_gui.cpp
  src/server/ServerGuiApp.cpp
  src/gui/GuiStyle.cpp
)
target_link_libraries(lan_jam_server_gui PRIVATE
  core
  imgui::imgui
  glad::glad
  glfw
)

if (WIN32)
  target_link_libraries(lan_jam_server_gui PRIVATE opengl32)
endif()
lanjam_copy_dll_from_vcpkg(lan_jam_server_gui rtaudio)
lanjam_copy_dll_from_vcpkg(lan_jam_server_gui glfw3)

if(LANJAM_ENABLE_CUDA)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  target_compile_definitions(core PUBLIC LANJAM_ENABLE_CUDA=1)
  add_library(lanjam_cuda_kernels OBJECT
    src/audio/CudaVoiceKernel.cu
  )
  target_include_directories(lanjam_cuda_kernels PRIVATE src)
  target_compile_definitions(lanjam_cuda_kernels PRIVATE LANJAM_ENABLE_CUDA=1)
  set_target_properties(lanjam_cuda_kernels PROPERTIES
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON
    CUDA_ARCHITECTURES native
  )
  set_property(TARGET core PROPERTY CUDA_ARCHITECTURES native)
  target_sources(core PRIVATE $<TARGET_OBJECTS:lanjam_cuda_kernels>)
  target_link_libraries(core PRIVATE CUDA::cudart)
endif()
