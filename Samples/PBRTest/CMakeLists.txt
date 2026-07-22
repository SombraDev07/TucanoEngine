add_executable(PBRTest main.cpp)
target_link_libraries(PBRTest PRIVATE TucanoRuntime)
add_custom_command(TARGET PBRTest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PBRTest>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:PBRTest>/shaders"
)
tucano_copy_aftermath_dll(PBRTest)
