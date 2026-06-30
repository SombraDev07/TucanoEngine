# Build-time Mono AOT compilation helpers.

# Reads the list of .NET BCL assemblies deployed with the application from RequiredNETAssemblies.txt,
# ignoring blank lines and '#' comments.
#
# @param	outputVariable	Output list of assembly base names, without the '.dll' extension.
function(B3DReadRequiredNETAssemblies outputVariable)
	set(assemblyListFile "${B3D_FRAMEWORK_SOURCE_FOLDER}/CMake/RequiredNETAssemblies.txt")
	file(STRINGS "${assemblyListFile}" fileLines)

	set(assemblyNames "")
	foreach(line ${fileLines})
		string(REGEX REPLACE "#.*$" "" line "${line}")   # strip trailing comment
		string(STRIP "${line}" line)

		if(NOT line STREQUAL "")
			list(APPEND assemblyNames "${line}")
		endif()
	endforeach()

	set(${outputVariable} "${assemblyNames}" PARENT_SCOPE)
endfunction()

# Attaches post-build steps to a managed-assembly target that AOT-compile the BCL and the target's
# own assembly into the per-configuration assemblies folder (bin/Assemblies/{Debug|Release}), for
# AOT+interpreter mode. Does nothing unless B3D_MONO_AOT is enabled.
#
# @param	target					Managed assembly target to attach the post-build steps to, e.g. MBansheeEngine.
# @param	managedAssemblyFileName	File name of @p target's output assembly, e.g. "MBansheeEngine.dll". It must
#									already have been copied into the per-configuration assemblies folder by the
#									target's own post-build step before this function is called.
function(B3DAddPostBuildAOTCompileIfNeeded target managedAssemblyFileName)
	if(NOT B3D_MONO_AOT)
		return()
	endif()

	set(debugCompilerPath "${DotNETCoreMono_INSTALL_DIR}/bin/Debug/mono-aot-cross${CMAKE_EXECUTABLE_SUFFIX}")
	set(releaseCompilerPath "${DotNETCoreMono_INSTALL_DIR}/bin/Release/mono-aot-cross${CMAKE_EXECUTABLE_SUFFIX}")

	if(NOT EXISTS "${debugCompilerPath}" AND NOT EXISTS "${releaseCompilerPath}")
		message(WARNING "[MonoAOT] B3D_MONO_AOT is ON but no mono-aot-cross was found under ${DotNETCoreMono_INSTALL_DIR}/bin/<Config>/. AOT images will NOT be generated; ")
		return()
	endif()

	# Configuration-matched paths (Debug build -> Debug, every other config -> Release)
	set(destinationAssemblyFolder "${PROJECT_BINARY_DIR}/bin/Assemblies/$<IF:$<CONFIG:Debug>,Debug,Release>")
	set(compilerExecutablePath "${DotNETCoreMono_INSTALL_DIR}/bin/$<IF:$<CONFIG:Debug>,Debug,Release>/mono-aot-cross${CMAKE_EXECUTABLE_SUFFIX}")
	set(sourceAssemblyFolder "${DotNETCoreMono_INSTALL_DIR}/bin/Assemblies")
	set(compileScriptPath "${B3D_FRAMEWORK_SOURCE_FOLDER}/CMake/RunMonoAotCompiler.cmake")

	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${destinationAssemblyFolder}"
		COMMENT "[MonoAOT] Preparing ${destinationAssemblyFolder}")

	# Stage + AOT each BCL assembly
	B3DReadRequiredNETAssemblies(bclAssemblies)
	foreach(assemblyName ${bclAssemblies})
		set(isMandatory FALSE)
		if(assemblyName STREQUAL "System.Private.CoreLib")
			set(isMandatory TRUE)
		endif()

		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND}
				"-DCOMPILER_EXECUTABLE_PATH=${compilerExecutablePath}"
				"-DSOURCE_ASSEMBLY_FOLDER=${sourceAssemblyFolder}"
				"-DDESTINATION_ASSEMBLY_FOLDER=${destinationAssemblyFolder}"
				"-DASSEMBLY_FILE_NAME=${assemblyName}.dll"
				"-DIS_MANDATORY=${isMandatory}"
				-P "${compileScriptPath}"
			COMMENT "[MonoAOT] BCL ${assemblyName}.dll"
			VERBATIM)
	endforeach()

	# AOT the managed assembly itself (already copied into destinationAssemblyFolder by the target's own POST_BUILD).
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND}
			"-DCOMPILER_EXECUTABLE_PATH=${compilerExecutablePath}"
			"-DSOURCE_ASSEMBLY_FOLDER="
			"-DDESTINATION_ASSEMBLY_FOLDER=${destinationAssemblyFolder}"
			"-DASSEMBLY_FILE_NAME=${managedAssemblyFileName}"
			"-DIS_MANDATORY=FALSE"
			-P "${compileScriptPath}"
		COMMENT "[MonoAOT] ${managedAssemblyFileName}"
		VERBATIM)
endfunction()
