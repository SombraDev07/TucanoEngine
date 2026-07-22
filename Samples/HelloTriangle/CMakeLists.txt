add_executable(HelloTriangle main.cpp)
target_link_libraries(HelloTriangle PRIVATE TucanoRuntime)
add_custom_command(TARGET HelloTriangle POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HelloTriangle>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:HelloTriangle>/shaders"
)
tucano_copy_aftermath_dll(HelloTriangle)
