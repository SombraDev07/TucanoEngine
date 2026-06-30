# Source files and their filters
B3DGlobSourceFiles(${CMAKE_CURRENT_SOURCE_DIR} "" "" BS_RENDERBEAST_SRC)

# Target
B3DAddPlugin(bsfRenderBeast ENGINE ${BS_RENDERBEAST_SRC})

# Common flags
B3DSetDefaultLinkAndCompileFlags(bsfRenderBeast)

# Includes
target_include_directories(bsfRenderBeast PRIVATE "./")

# IDE specific
set_property(TARGET bsfRenderBeast PROPERTY FOLDER Plugins)

# NRD (NVIDIA Real-time Denoiser) - built when the NRD dependency is available
if(TARGET NRD)
	find_package(Vulkan QUIET)
	target_include_directories(bsfRenderBeast PRIVATE
		${B3D_FRAMEWORK_ROOT_FOLDER}/Dependencies/NRD/Include
		${B3D_FRAMEWORK_ROOT_FOLDER}/Dependencies/NRD/Integration
		${B3D_FRAMEWORK_ROOT_FOLDER}/Source/Plugins/bsfVulkanGpuBackend
		${Vulkan_INCLUDE_DIRS}
	)
	if(TARGET NRI_VK)
		get_target_property(NRI_INC NRI_VK INTERFACE_INCLUDE_DIRECTORIES)
		if(NRI_INC)
			target_include_directories(bsfRenderBeast PRIVATE ${NRI_INC})
		endif()
	endif()
	target_link_libraries(bsfRenderBeast PRIVATE NRD)
	if(TARGET NRDIntegration)
		target_link_libraries(bsfRenderBeast PRIVATE NRDIntegration)
	endif()
	if(TARGET NRI_VK)
		target_link_libraries(bsfRenderBeast PRIVATE NRI_VK NRI_Shared)
	endif()
	target_compile_definitions(bsfRenderBeast PRIVATE B3D_ENABLE_NRD=1)
endif()

# Procedural sky (Preetham analytic sky model) - always enabled since the shader is built-in.
target_compile_definitions(bsfRenderBeast PRIVATE B3D_ENABLE_SKY_PROCEDURAL=1)

# FidelityFX FSR3 integration. Enabled when the FFX SDK libraries are available.
option(B3D_FFX_FSR3 "Enable FidelityFX FSR3 upscaling" OFF)
if(B3D_FFX_FSR3)
	find_package(Vulkan REQUIRED)
	target_include_directories(bsfRenderBeast PRIVATE
		${B3D_FRAMEWORK_ROOT_FOLDER}/Dependencies/FidelityFX/sdk/include
		${B3D_FRAMEWORK_ROOT_FOLDER}/Source/Plugins/bsfVulkanGpuBackend
		${Vulkan_INCLUDE_DIRS}
	)
	target_link_directories(bsfRenderBeast PRIVATE ${B3D_FRAMEWORK_ROOT_FOLDER}/bin/ffx_sdk)
	target_link_libraries(bsfRenderBeast PRIVATE
		ffx_fsr3_x64
		ffx_fsr3upscaler_x64
		ffx_backend_vk_x64
	)
	target_compile_definitions(bsfRenderBeast PRIVATE B3D_ENABLE_FSR3=1)
endif()

# Install
if(B3D_RENDERER MATCHES "RenderBeast" OR B3D_BUILD_ALL_PLUGINS)
	B3DSetUpPostBuildAndInstallSteps(bsfRenderBeast)
endif()
