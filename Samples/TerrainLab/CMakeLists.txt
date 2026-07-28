add_executable(TerrainLab main.cpp)
target_include_directories(TerrainLab PRIVATE "${CMAKE_SOURCE_DIR}/Samples")
target_link_libraries(TerrainLab PRIVATE TucanoRuntime lua libcurl)
add_custom_command(TARGET TerrainLab POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TerrainLab>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:TerrainLab>/shaders"
)
tucano_copy_aftermath_dll(TerrainLab)
