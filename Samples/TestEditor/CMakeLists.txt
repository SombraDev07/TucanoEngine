add_executable(TestEditor main.cpp)
target_include_directories(TestEditor PRIVATE "${CMAKE_SOURCE_DIR}/Samples")
target_link_libraries(TestEditor PRIVATE TucanoRuntime)
add_custom_command(TARGET TestEditor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TestEditor>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:TestEditor>/shaders"
)
tucano_copy_aftermath_dll(TestEditor)
tucano_deploy_testplay(TestEditor)
