# Called from POST_BUILD: copy a vcpkg DLL next to the built executable if it exists.
# -D DLL_DBG=.../debug/bin/foo.dll
# -D DLL_REL=.../bin/foo.dll
# -D OUT_DIR=<TARGET_FILE_DIR>
# -D DLL_NAME=foo.dll
# -D CFG=$<CONFIG> (expanded by the build system)
if(NOT DEFINED DLL_DBG OR NOT DEFINED DLL_REL OR NOT DEFINED OUT_DIR OR NOT DEFINED DLL_NAME OR NOT DEFINED CFG)
  message(FATAL_ERROR "CopyVcpkgDll.cmake: missing DLL_DBG/DLL_REL/OUT_DIR/DLL_NAME/CFG")
endif()
if(CFG STREQUAL "Debug")
  if(EXISTS "${DLL_DBG}")
    set(_src "${DLL_DBG}")
  elseif(EXISTS "${DLL_REL}")
    # Some vcpkg layouts only install one shared DLL under bin/ (used for Debug too).
    set(_src "${DLL_REL}")
  else()
    set(_src "${DLL_DBG}")
  endif()
else()
  set(_src "${DLL_REL}")
endif()
if(EXISTS "${_src}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_src}" "${OUT_DIR}/${DLL_NAME}"
    RESULT_VARIABLE _rv)
  if(NOT _rv EQUAL 0)
    message(FATAL_ERROR "CopyVcpkgDll.cmake: copy failed ${_src} -> ${OUT_DIR}/${DLL_NAME}")
  endif()
else()
  message(WARNING "CopyVcpkgDll.cmake: DLL not found (skipped): ${_src}")
endif()
