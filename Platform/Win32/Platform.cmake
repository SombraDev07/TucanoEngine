# Each platform contributes to the GPU backend collectors:
#   B3D_GPU_BACKEND_CHOICES   - list of selectable backend names (cache STRINGS)
#   B3D_GPU_BACKEND_DEFAULT   - default backend when the user hasn't chosen one
#   B3D_GPU_BACKEND_LIB_<name>- maps a backend name to its plugin target
if(WIN32)
	list(APPEND B3D_GPU_BACKEND_CHOICES Vulkan Null)
	set(B3D_GPU_BACKEND_DEFAULT Vulkan)

	set(B3D_GPU_BACKEND_LIB_Vulkan bsfVulkanGpuBackend)
	set(B3D_GPU_BACKEND_LIB_Null bsfNullGpuBackend)
endif()
