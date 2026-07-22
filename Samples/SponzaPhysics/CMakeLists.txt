add_executable(SponzaPhysics
  main.cpp
)
target_link_libraries(SponzaPhysics PRIVATE TucanoRuntime)
tucano_copy_aftermath_dll(SponzaPhysics)
