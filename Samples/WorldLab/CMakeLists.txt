add_executable(WorldLab main.cpp)
target_include_directories(WorldLab PRIVATE "${CMAKE_SOURCE_DIR}/Samples")
target_link_libraries(WorldLab PRIVATE TucanoRuntime lua libcurl)
add_custom_command(TARGET WorldLab POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WorldLab>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:WorldLab>/shaders"
)
tucano_copy_aftermath_dll(WorldLab)
