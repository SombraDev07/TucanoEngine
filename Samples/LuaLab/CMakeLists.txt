add_executable(LuaLab main.cpp)
target_link_libraries(LuaLab PRIVATE TucanoRuntime lua libcurl)
add_custom_command(TARGET LuaLab POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LuaLab>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:LuaLab>/shaders"
)
tucano_copy_aftermath_dll(LuaLab)
