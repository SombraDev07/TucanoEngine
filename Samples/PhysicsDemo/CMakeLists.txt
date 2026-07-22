add_executable(PhysicsDemo
  main.cpp
)
target_link_libraries(PhysicsDemo PRIVATE TucanoRuntime)
tucano_copy_aftermath_dll(PhysicsDemo)
