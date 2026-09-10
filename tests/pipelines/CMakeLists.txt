# Pipeline integration is opt-in: FileInfo and FileMerger are installed products.
set(DATA_DIR_TOPPAS "${CMAKE_CURRENT_SOURCE_DIR}")
configure_file(ExecutePipeline_1.trf.in ExecutePipeline_1.trf)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/output")
add_test(NAME TOPP_ExecutePipeline_1 COMMAND ExecutePipeline -test
  -in "${CMAKE_CURRENT_SOURCE_DIR}/ExecutePipeline_1.toppas"
  -resource_file "${CMAKE_CURRENT_BINARY_DIR}/ExecutePipeline_1.trf"
  -out_dir "${CMAKE_CURRENT_BINARY_DIR}/output")
set_tests_properties(TOPP_ExecutePipeline_1 PROPERTIES TIMEOUT 300)
