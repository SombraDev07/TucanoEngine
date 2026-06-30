# Find Vulkan installation
#
# This module defines
#  Vulkan_INCLUDE_DIRS
#  Vulkan_LIBRARIES
#  Vulkan_FOUND

B3DStartFindPackage(Vulkan)

set(Vulkan_INSTALL_DIR "$ENV{VULKAN_SDK}" CACHE PATH "")
B3DPopulateDefaultPackageSearchPaths(Vulkan)

if(WIN32)
	set(Vulkan_LIBNAME vulkan-1)
	list(APPEND Vulkan_INCLUDE_SEARCH_DIRS "${Vulkan_INSTALL_DIR}/Include")

	if(B3D_IS_64BIT)
		list(APPEND Vulkan_LIBRARY_RELEASE_SEARCH_DIRS "${Vulkan_INSTALL_DIR}/Bin")
		list(APPEND Vulkan_LIBRARY_DEBUG_SEARCH_DIRS "${Vulkan_INSTALL_DIR}/Bin")
	else()
		list(APPEND Vulkan_LIBRARY_RELEASE_SEARCH_DIRS "${Vulkan_INSTALL_DIR}/Bin32")
		list(APPEND Vulkan_LIBRARY_DEBUG_SEARCH_DIRS "${Vulkan_INSTALL_DIR}/Bin32")
	endif()
else()
	set(Vulkan_LIBNAME vulkan)
endif()

B3DFindImportedIncludes(Vulkan vulkan/vulkan.h)
B3DFindImportedLibrary(Vulkan ${Vulkan_LIBNAME} SHARED)

B3DEndFindPackage(Vulkan ${Vulkan_LIBNAME})

