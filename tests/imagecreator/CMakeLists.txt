file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/output")
foreach(_case 1 2)
  set(_options)
  if(_case EQUAL 2)
    set(_options -out_type bmp -precursors -precursor_size 3 -precursor_color green -log_intensity)
  endif()
  set(_output "${CMAKE_CURRENT_BINARY_DIR}/output/ImageCreator_${_case}.bmp")
  add_test(NAME TOPP_ImageCreator_${_case} COMMAND ImageCreator -test
    -in "${CMAKE_CURRENT_SOURCE_DIR}/ImageCreator_${_case}_input.mzML"
    -out "${_output}" -width 20 -height 15 ${_options})
  add_test(NAME TOPP_ImageCreator_${_case}_out1 COMMAND ${CMAKE_COMMAND} -E compare_files
    "${_output}" "${CMAKE_CURRENT_SOURCE_DIR}/ImageCreator_${_case}_output.bmp")
  set_tests_properties(TOPP_ImageCreator_${_case}_out1 PROPERTIES DEPENDS TOPP_ImageCreator_${_case})
endforeach()
