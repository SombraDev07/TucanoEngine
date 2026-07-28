#pragma once

#include "Vegetation/VegetationRenderer.h"
#include "RHI/RHI.h"
#include <glm/glm.hpp>

namespace tucano::veg {

struct VegDispatch {
	/// Optional hiZ: previous-frame occlusion mip (R32_FLOAT). Null disables Hi-Z.
	static void recordDispatch(rhi::Device& device, rhi::CommandList& cmd, VegetationRenderer& veg,
	                           const glm::mat4& viewProj, const glm::vec3& cameraPos, float maxDist,
	                           rhi::Texture* hiZ = nullptr, uint32_t screenW = 1, uint32_t screenH = 1);
};

} // namespace tucano::veg
