# SPDX-License-Identifier: BSD-3-Clause
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/DesktopDependencies.cmake")

function(openms_desktop_application name)
  set(_source "${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp")
  if(APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}-resources/${name}.icns")
    set(_icon "${CMAKE_CURRENT_SOURCE_DIR}/${name}-resources/${name}.icns")
    add_executable(${name} MACOSX_BUNDLE "${_source}" "${_icon}")
    set_source_files_properties("${_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(${name} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/${name}-resources/${name}.plist.in"
      MACOSX_BUNDLE_ICON_FILE "${name}.icns"
      MACOSX_BUNDLE_GUI_IDENTIFIER "de.openms.${name}"
      MACOSX_BUNDLE_BUNDLE_NAME "${name}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_INFO_STRING "${name} ${PROJECT_VERSION}"
      MACOSX_BUNDLE_COPYRIGHT "Copyright The OpenMS Team")
    set(_relative_path "${CMAKE_INSTALL_BINDIR}/${name}.app/Contents/MacOS/${name}")
  elseif(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}.rc")
    enable_language(RC)
    add_executable(${name} "${_source}" "${CMAKE_CURRENT_SOURCE_DIR}/${name}.rc")
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_relative_path "${CMAKE_INSTALL_BINDIR}/${name}${CMAKE_EXECUTABLE_SUFFIX}")
  else()
    add_executable(${name} "${_source}")
    set(_relative_path "${CMAKE_INSTALL_BINDIR}/${name}${CMAKE_EXECUTABLE_SUFFIX}")
  endif()
  target_compile_features(${name} PRIVATE cxx_std_23)
  target_link_libraries(${name} PRIVATE OpenMS::GUI OpenMS::CLI OpenMS::Core)
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")
  install(TARGETS ${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})
  set(${name}_MANIFEST_PATH "${_relative_path}" PARENT_SCOPE)
endfunction()

function(openms_desktop_manifest package category)
  set(_content "# name\tcategory\tproduct version\trelative executable path\n")
  set(_build_content "${_content}")
  foreach(_name IN LISTS ARGN)
    set(_category "${category}")
    if(DEFINED ${_name}_MANIFEST_CATEGORY)
      set(_category "${${_name}_MANIFEST_CATEGORY}")
    endif()
    string(APPEND _content "${_name}\t${_category}\t${PROJECT_VERSION}\t${${_name}_MANIFEST_PATH}\n")
    # Build prefixes use bin regardless of an install-time GNUInstallDirs override.
    if(APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_name}-resources/${_name}.icns")
      set(_build_path "bin/${_name}.app/Contents/MacOS/${_name}")
    else()
      set(_build_path "bin/${_name}${CMAKE_EXECUTABLE_SUFFIX}")
    endif()
    string(APPEND _build_content "${_name}\t${_category}\t${PROJECT_VERSION}\t${_build_path}\n")
  endforeach()
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/share/openms4/tools/${package}.tools.tsv" CONTENT "${_build_content}")
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${package}.install.tools.tsv" CONTENT "${_content}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${package}.install.tools.tsv"
    DESTINATION share/openms4/tools RENAME "${package}.tools.tsv")
endfunction()
