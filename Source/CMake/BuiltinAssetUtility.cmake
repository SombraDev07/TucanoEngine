#######################################################################################
##################### Built-in asset import and versioning ############################
#######################################################################################

# Registers a target that performs asset import for builtin assets.
#
# @param	assetPrefix			Prefix identifying the asset pack to build (e.g. 'Framework', 'Editor', etc.)
# @param	inputFolder			Absolute folder in which to find raw assets to import.
# @param	outputFolder		Absolute folder in which to place imported assets.
# @param	additionalArguments	Optional additional arguments to pass to the import tool.
function(B3DAddRunAssetImportTarget assetPrefix inputFolder outputFolder additionalArguments)
	if(NOT TARGET BansheeImportTool)
		message(WARNING "Cannot add asset import target: BansheeImportTool target not found")
		return()
	endif()

	set(RunAssetImport_INPUT_FOLDER ${inputFolder})
	set(RunAssetImport_OUTPUT_FOLDER ${outputFolder})
	set(RunAssetImport_CMD_ARGS ${additionalArguments})
	set(RunAssetImport_PREFIX ${assetPrefix})

	configure_file(
		${B3D_FRAMEWORK_SOURCE_FOLDER}/CMake/Scripts/RunAssetImport.cmake.in
		${CMAKE_CURRENT_BINARY_DIR}/RunAssetImport_${assetPrefix}.cmake
		@ONLY)

	add_custom_target(RunAssetImport_${assetPrefix}
		COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/RunAssetImport_${assetPrefix}.cmake -- "$<TARGET_FILE:BansheeImportTool>"
		DEPENDS BansheeImportTool
		COMMENT "Running asset import for ${assetPrefix}...")

	set_property(TARGET RunAssetImport_${assetPrefix} PROPERTY FOLDER Scripts)
endfunction()
