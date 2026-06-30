# Conditionally AOT-compiles a single managed assembly with mono-aot-cross.
#
# It optionally stages the assembly from a source directory first, then AOT-compiles it only when
# the resulting image is missing or out of date.
#
# Required -D arguments:
#   COMPILER_EXECUTABLE_PATH    - full path to the mono-aot-cross executable
#   DESTINATION_ASSEMBLY_FOLDER - directory the assembly lives in; also used as MONO_PATH and the
#                                 working directory (so the emitted <assembly>.dll image lands next
#                                 to the managed dll)
#   ASSEMBLY_FILE_NAME          - assembly file name, e.g. "System.Private.CoreLib.dll"
#   IS_MANDATORY                - TRUE to make a missing assembly or a failed compile fatal (corlib)
# Optional:
#   SOURCE_ASSEMBLY_FOLDER      - if set and contains ASSEMBLY_FILE_NAME, the assembly is copied into
#                                 DESTINATION_ASSEMBLY_FOLDER first. Leave empty for assemblies already
#                                 present in DESTINATION_ASSEMBLY_FOLDER.

if(DEFINED SOURCE_ASSEMBLY_FOLDER AND NOT SOURCE_ASSEMBLY_FOLDER STREQUAL "" AND EXISTS "${SOURCE_ASSEMBLY_FOLDER}/${ASSEMBLY_FILE_NAME}")
	execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE_ASSEMBLY_FOLDER}/${ASSEMBLY_FILE_NAME}" "${DESTINATION_ASSEMBLY_FOLDER}/")
endif()

set(inputAssemblyPath "${DESTINATION_ASSEMBLY_FOLDER}/${ASSEMBLY_FILE_NAME}")
# Mono names the AOT image "<assembly-file-name><shared-lib-ext>"; on Windows that is "<name>.dll.dll".
set(outputImagePath "${inputAssemblyPath}.dll")

if(NOT EXISTS "${inputAssemblyPath}")
	if(IS_MANDATORY)
		message(FATAL_ERROR "[MonoAOT] Required assembly is missing: ${inputAssemblyPath}")
	endif()
	message(STATUS "[MonoAOT] Skipping (assembly not present): ${ASSEMBLY_FILE_NAME}")
	return()
endif()

# Skip the (slow) compile when the image already exists and is newer than both the source assembly and the cross compiler itself.
if(EXISTS "${outputImagePath}" AND "${outputImagePath}" IS_NEWER_THAN "${inputAssemblyPath}" AND "${outputImagePath}" IS_NEWER_THAN "${COMPILER_EXECUTABLE_PATH}")
	return()
endif()

message(STATUS "[MonoAOT] AOT-compiling ${ASSEMBLY_FILE_NAME}")
execute_process(
	COMMAND "${CMAKE_COMMAND}" -E env "MONO_PATH=${DESTINATION_ASSEMBLY_FOLDER}"
		"${COMPILER_EXECUTABLE_PATH}" --aot=interp "${ASSEMBLY_FILE_NAME}"
	WORKING_DIRECTORY "${DESTINATION_ASSEMBLY_FOLDER}"
	RESULT_VARIABLE compileResult
)

if(NOT compileResult EQUAL 0)
	if(IS_MANDATORY)
		message(FATAL_ERROR "[MonoAOT] AOT compilation failed for ${ASSEMBLY_FILE_NAME} (exit ${compileResult}). This assembly is required for mode 5; aborting.")
	else()
		message(WARNING "[MonoAOT] AOT compilation failed for ${ASSEMBLY_FILE_NAME} (exit ${compileResult}); the interpreter will execute it at runtime.")
	endif()
endif()
