add_executable(SponzaViewer main.cpp)
target_link_libraries(SponzaViewer PRIVATE TucanoRuntime)
add_custom_command(TARGET SponzaViewer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SponzaViewer>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:SponzaViewer>/shaders"
)
tucano_copy_aftermath_dll(SponzaViewer)
