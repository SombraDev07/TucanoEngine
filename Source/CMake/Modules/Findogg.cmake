# Find ogg dependency
#
# This module defines
#  ogg_INCLUDE_DIRS
#  ogg_LIBRARIES
#  ogg_FOUND

B3DStartFindPackage(ogg)

set(ogg_BUNDLED_INSTALL_DIR ${B3D_FRAMEWORK_SOURCE_FOLDER}/../Dependencies/libogg)
if(B3D_USE_BUNDLED_LIBRARIES OR NOT ogg_INSTALL_DIR)
	set(ogg_INSTALL_DIR ${ogg_BUNDLED_INSTALL_DIR} CACHE PATH "Path to ogg dependency" FORCE)
endif()
B3DPopulateDefaultPackageSearchPaths(ogg)

B3DFindImportedIncludes(ogg ogg/ogg.h)

if(WIN32)
	B3DFindImportedLibraryWithConfigurationNames(ogg ogg STATIC ogg oggd)
else()
	B3DFindImportedLibrary(ogg ogg STATIC)
endif()

B3DEndFindPackage(ogg ogg)
