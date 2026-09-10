cmake_minimum_required(VERSION 3.24)
project(OpenMSGUI VERSION 1.0.0 LANGUAGES CXX)
include(CTest)
set(OPENMS4_DEPENDENCY_LOCK_FILE "${CMAKE_CURRENT_LIST_DIR}/../dependencies.lock.json")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/DesktopDependencies.cmake")
if(BUILD_TESTING)
  openms4_find_core(COMPONENTS TestSupport)
else()
  openms4_find_core()
endif()
openms4_find_package(OpenMSCLI)
openms4_source_revision(OpenMSGUI_SOURCE_REVISION)

option(OPENMS_GUI_WEBENGINE "Enable Qt WebEngine sequence views when available" ON)
set(_qt_components Core Gui Widgets Svg OpenGLWidgets)
if(APPLE)
  list(APPEND _qt_components PrintSupport)
endif()
find_package(Qt6 6.1 REQUIRED COMPONENTS ${_qt_components})
if(OPENMS_GUI_WEBENGINE)
  find_package(Qt6 6.1 QUIET COMPONENTS WebEngineWidgets)
  if(TARGET Qt6::WebEngineWidgets)
    list(APPEND _qt_components WebEngineWidgets)
  endif()
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/includes.cmake")
list(REMOVE_DUPLICATES OpenMSVisual_sources)
list(REMOVE_DUPLICATES OpenMSVisual_sources_h)
add_library(OpenMS_GUI SHARED ${OpenMSVisual_sources})
add_library(OpenMS::GUI ALIAS OpenMS_GUI)
set_target_properties(OpenMS_GUI PROPERTIES
  EXPORT_NAME GUI VERSION ${PROJECT_VERSION} SOVERSION 1
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<0:>")
target_compile_features(OpenMS_GUI PUBLIC cxx_std_23)
target_link_libraries(OpenMS_GUI PUBLIC OpenMS::Core PRIVATE OpenMS::CLI)
foreach(_component IN LISTS _qt_components)
  target_link_libraries(OpenMS_GUI PUBLIC Qt6::${_component})
endforeach()
target_include_directories(OpenMS_GUI PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
include(GenerateExportHeader)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/OpenMS/VISUAL")
generate_export_header(OpenMS_GUI
  EXPORT_MACRO_NAME OPENMS_GUI_DLLAPI
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/OpenMS/VISUAL/OpenMS_GUIConfig.h")
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_multi_config)
  set(_autogen_include "${CMAKE_CURRENT_BINARY_DIR}/OpenMS_GUI_autogen/include_$<CONFIG>")
else()
  set(_autogen_include "${CMAKE_CURRENT_BINARY_DIR}/OpenMS_GUI_autogen/include")
endif()
# Private generated widget types are needed by GUI's own white-box tests only.
target_include_directories(OpenMS_GUI INTERFACE "$<BUILD_INTERFACE:${_autogen_include}>")
if(MSVC)
  target_compile_options(OpenMS_GUI PRIVATE /we4100 /we4189)
  target_link_options(OpenMS_GUI PRIVATE /FORCE:MULTIPLE /INCREMENTAL:NO /ignore:4006 /ignore:4088)
endif()
install(TARGETS OpenMS_GUI EXPORT OpenMSGUITargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/OpenMS/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/OpenMS
  FILES_MATCHING PATTERN "*.h")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/OpenMS/VISUAL/OpenMS_GUIConfig.h"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/OpenMS/VISUAL)
include(CMakePackageConfigHelpers)
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/OpenMSGUIConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/OpenMSGUIConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenMSGUI)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/OpenMSGUIConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(EXPORT OpenMSGUITargets NAMESPACE OpenMS::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenMSGUI)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/OpenMSGUIConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/OpenMSGUIConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenMSGUI)
if(BUILD_TESTING)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../tests/class_tests" "${CMAKE_CURRENT_BINARY_DIR}/tests")
endif()
