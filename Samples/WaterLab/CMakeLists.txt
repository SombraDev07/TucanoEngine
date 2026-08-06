add_executable(WaterLab main.cpp)
target_include_directories(WaterLab PRIVATE "${CMAKE_SOURCE_DIR}/Samples")
target_link_libraries(WaterLab PRIVATE TucanoRuntime lua libcurl)
add_custom_command(TARGET WaterLab POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WaterLab>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:WaterLab>/shaders"
)
tucano_copy_aftermath_dll(WaterLab)
tucano_deploy_testplay(WaterLab)
