add_executable(TucanoBenchmark main.cpp)
target_link_libraries(TucanoBenchmark PRIVATE TucanoRuntime)
add_custom_command(TARGET TucanoBenchmark POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TucanoBenchmark>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:TucanoBenchmark>/shaders"
)
