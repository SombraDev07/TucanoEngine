# Find vorbis dependency
#
# This module defines
#  vorbis_INCLUDE_DIRS
#  vorbis_LIBRARIES
#  vorbis_FOUND

B3DStartFindPackage(vorbis)

set(vorbis_BUNDLED_INSTALL_DIR ${B3D_FRAMEWORK_SOURCE_FOLDER}/../Dependencies/libvorbis)
if(B3D_USE_BUNDLED_LIBRARIES OR NOT vorbis_INSTALL_DIR)
	set(vorbis_INSTALL_DIR ${vorbis_BUNDLED_INSTALL_DIR} CACHE PATH "Path to vorbis dependency" FORCE)
endif()
B3DPopulateDefaultPackageSearchPaths(vorbis)

B3DFindImportedIncludes(vorbis vorbis/vorbisenc.h)

if(WIN32)
	B3DFindImportedLibraryWithConfigurationNames(vorbis vorbis STATIC vorbis vorbisd)
	B3DFindImportedLibraryWithConfigurationNames(vorbis vorbisfile STATIC vorbisfile vorbisfiled)
	B3DFindImportedLibraryWithConfigurationNames(vorbis vorbisenc STATIC vorbisenc vorbisencd)
else()
	B3DFindImportedLibrary(vorbis vorbisfile STATIC)
	B3DFindImportedLibrary(vorbis vorbisenc STATIC)
	B3DFindImportedLibrary(vorbis vorbis STATIC)
endif()

B3DEndFindPackage(vorbis vorbis)
