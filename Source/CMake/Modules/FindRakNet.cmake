# Find RakNet dependency
#
# This module defines
#  RakNet_INCLUDE_DIRS
#  RakNet_LIBRARIES
#  RakNet_FOUND

B3DStartFindPackage(RakNet)

set(RakNet_BUNDLED_INSTALL_DIR ${B3D_FRAMEWORK_SOURCE_FOLDER}/../Dependencies/RakNet)
if(B3D_USE_BUNDLED_LIBRARIES OR NOT RakNet_INSTALL_DIR)
	set(RakNet_INSTALL_DIR ${RakNet_BUNDLED_INSTALL_DIR} CACHE PATH "Path to RakNet dependency" FORCE)
endif()

B3DPopulateDefaultPackageSearchPaths(RakNet)
B3DFindImportedIncludes(RakNet RakNet/RakPeer.h)
B3DFindImportedLibrary(RakNet RakNetLibStatic SHARED)

B3DEndFindPackage(RakNet RakNetLibStatic)
