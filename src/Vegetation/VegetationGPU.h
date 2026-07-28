#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace tucano::veg::gpu {

static_assert(sizeof(glm::mat4) == 64, "mat4 must be 64 bytes");
static_assert(sizeof(glm::vec4) == 16, "vec4 must be 16 bytes");

struct alignas(16) VegInstance {
	glm::mat4 worldMatrix{1.0f};
	glm::vec3 center{0};
	float radius = 1.0f;
	uint32_t typeId = 0;
	float windFlex = 0.5f;
	float windHeight = 1.0f;
	float windPhase = 0.0f;
};
static_assert(sizeof(VegInstance) == 96, "VegInstance must be 96 bytes");

struct VegDrawArgs {
	uint32_t indexCountPerInstance = 0;
	uint32_t instanceCount = 0;
	uint32_t startIndexLocation = 0;
	int32_t baseVertexLocation = 0;
	uint32_t startInstanceLocation = 0;
};
static_assert(sizeof(VegDrawArgs) == sizeof(uint32_t) * 5, "VegDrawArgs must be 20 bytes");

struct alignas(16) VegFrameConstants {
	glm::vec4 windDirection{1, 0, 0, 0};
	float time = 0;
	float strength = 1.0f;
	float gustStrength = 0.0f;
	float turbulence = 0.4f;
	uint32_t instanceCount = 0;
	uint32_t enableHiZ = 0;
	float screenWidth = 1.0f;
	float screenHeight = 1.0f;

	glm::vec4 frustumPlanes[6]{};
	glm::vec4 observer{0};
	float maxDistance = 150.0f;
	float nearPlane = 0.1f;
	float densityScale = 1.0f;
	float ditherFrame = 0;
	uint32_t enableLODCrossFade = 1;
	float crossFadeWidth = 0.15f;
	uint32_t _pad3 = 0;
	uint32_t _pad4 = 0;
	glm::vec4 lodDistances{30.f, 80.f, 150.f, 200.f};
	glm::mat4 viewProj{1.0f};
};
static_assert(sizeof(VegFrameConstants) == 272, "VegFrameConstants must be 272 bytes");

struct alignas(16) LODRangeGPU {
	float distances[4];
	float crossFadeWidth;
	float _pad0;
	float _pad1;
	float _pad2;
};
static_assert(sizeof(LODRangeGPU) == 32, "LODRangeGPU must be 32 bytes");

struct alignas(16) BillboardConstants {
	float invAtlasSize = 1.0f / 1024.0f;
	uint32_t atlasWidth = 1024;
	uint32_t atlasHeight = 1024;
	uint32_t viewsPerType = 8;
	uint32_t gridSize = 16;
	uint32_t _pad0 = 0;
	uint32_t _pad1 = 0;
	uint32_t _pad2 = 0;
};
static_assert(sizeof(BillboardConstants) == 32, "BillboardConstants size");

} // namespace tucano::veg::gpu
