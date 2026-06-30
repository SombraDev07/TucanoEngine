# Target
if(WIN32)
	add_executable(Terrain WIN32 "Main.cpp")
else()
	add_executable(Terrain "Main.cpp")
endif()

B3DSetDefaultLinkAndCompileFlags(Terrain)

# Working directory
set_target_properties(Terrain PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")

# Libraries
## Local libs
target_link_libraries(Terrain Common)

# Plugin dependencies
B3DAddRuntimeDependencies(Terrain)
add_dependencies(Terrain bsfFBXImporter bsfFontImporter bsfFreeImgImporter)

# Install
B3DSetUpPostBuildAndInstallSteps(Terrain)

# IDE specific
set_property(TARGET Terrain PROPERTY FOLDER Examples)