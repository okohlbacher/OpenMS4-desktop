# SPDX-License-Identifier: BSD-3-Clause
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/OpenMS4Dependencies.cmake")
include(GNUInstallDirs)
cmake_path(SET OPENMS4_DEPENDENCY_LOCK_FILE NORMALIZE "${CMAKE_CURRENT_LIST_DIR}/../dependencies.lock.json")

# A viewer/workflow build from this repository consumes the GUI SDK from the
# same desktop revision. Keeping this out of dependencies.lock.json avoids a
# self-referential Git commit hash.
function(openms_desktop_find_gui)
  if(TARGET OpenMS::GUI)
    return()
  endif()
  openms4_source_revision(_expected_gui_revision)
  find_package(OpenMSGUI 1.0.0 EXACT CONFIG REQUIRED)
  _openms4_require_clean_sdk(OpenMSGUI)
  if(NOT "${OpenMSGUI_SOURCE_REVISION}" STREQUAL "${_expected_gui_revision}")
    message(FATAL_ERROR "GUI SDK must come from this desktop source revision: expected ${_expected_gui_revision}, got ${OpenMSGUI_SOURCE_REVISION}")
  endif()
endfunction()
