add_executable(PhysicsDemo
  main.cpp
)
target_link_libraries(PhysicsDemo PRIVATE TucanoRuntime lua libcurl)
tucano_copy_aftermath_dll(PhysicsDemo)
