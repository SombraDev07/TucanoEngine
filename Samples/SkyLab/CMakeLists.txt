add_executable(SkyLab main.cpp)
target_include_directories(SkyLab PRIVATE "${CMAKE_SOURCE_DIR}/Samples")
target_link_libraries(SkyLab PRIVATE TucanoRuntime)
add_custom_command(TARGET SkyLab POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SkyLab>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:SkyLab>/shaders"
)
tucano_copy_aftermath_dll(SkyLab)
tucano_deploy_testplay(SkyLab)
