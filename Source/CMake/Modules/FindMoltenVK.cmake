# Find MoltenVK installation
#
# This module defines
#  MoltenVK_INCLUDE_DIRS
#  MoltenVK_LIBRARIES
#  MoltenVK_FOUND

B3DStartFindPackage(MoltenVK)

set(MoltenVK_BUNDLED_INSTALL_DIR ${B3D_FRAMEWORK_SOURCE_FOLDER}/../Dependencies/MoltenVK)
if(B3D_USE_BUNDLED_LIBRARIES OR NOT MoltenVK_INSTALL_DIR)
	set(MoltenVK_INSTALL_DIR ${MoltenVK_BUNDLED_INSTALL_DIR} CACHE PATH "Path to MoltenVK dependency" FORCE)
endif()
B3DPopulateDefaultPackageSearchPaths(MoltenVK)

B3DFindImportedIncludes(MoltenVK MoltenVK/mvk_vulkan.h)
B3DFindImportedLibrary(MoltenVK MoltenVK STATIC)

B3DEndFindPackage(MoltenVK MoltenVK)
