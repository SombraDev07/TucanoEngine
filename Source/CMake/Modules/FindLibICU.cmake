# Find LibICU dependency
#
# This module defines
#  LibICU_INCLUDE_DIRS
#  LibICU_LIBRARIES
#  LibICU_FOUND

B3DStartFindPackage(LibICU)

set(LibICU_BUNDLED_INSTALL_DIR ${B3D_FRAMEWORK_SOURCE_FOLDER}/../Dependencies/libICU)
if(B3D_USE_BUNDLED_LIBRARIES OR NOT LibICU_INSTALL_DIR)
	set(LibICU_INSTALL_DIR ${LibICU_BUNDLED_INSTALL_DIR} CACHE PATH "Path to LibICU dependency" FORCE)
endif()
B3DPopulateDefaultPackageSearchPaths(LibICU)

B3DFindImportedIncludes(LibICU unicode/utypes.h)
B3DFindImportedLibrary(LibICU icudata STATIC)
B3DFindImportedLibrary(LibICU icuuc STATIC)

B3DEndFindPackage(LibICU icudata)
