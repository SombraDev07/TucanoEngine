# Find mono dependency
#
# This module defines
#  mono_INCLUDE_DIRS
#  mono_LIBRARIES
#  mono_FOUND

B3DStartFindPackage(mono)

set(mono_BUNDLED_INSTALL_DIR ${B3D_FRAMEWORK_SOURCE_FOLDER}/../Dependencies/mono)
if(B3D_USE_BUNDLED_LIBRARIES OR NOT mono_INSTALL_DIR)
	set(mono_INSTALL_DIR ${mono_BUNDLED_INSTALL_DIR} CACHE PATH "Path to mono dependency" FORCE)
endif()

B3DPopulateDefaultPackageSearchPaths(mono)
list(APPEND mono_INCLUDE_SEARCH_DIRS ${mono_INSTALL_DIR}/include/mono-2.0 /usr/include/mono-2.0)

B3DFindImportedIncludes(mono mono/jit/jit.h)
B3DFindImportedLibraryWithAlternateBinaryName(mono mono-2.0 SHARED mono-2.0-sgen)

B3DEndFindPackage(mono mono-2.0)

# Install the managed libraries and config files required by Mono
install(
	DIRECTORY ${mono_INSTALL_DIR}/bin/Mono
	DESTINATION bin/
)
