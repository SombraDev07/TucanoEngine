#pragma once

#include <cstdint>
#include <glm/glm.hpp>

// -----------------------------------------------------------------------------
// DXR2 COMPRESSED1 vertex format — based on Esoterica's vertex compression.
//
// Positions: 16-bit unsigned offsets from a per-cluster anchor with a shared
// exponent. This is the same encoding as DXR2 COMPRESSED1, so vertex data is
// directly compatible with future DXR2 ray tracing.
//
// Normals: 16-bit signed normalized values.
//
// Static mesh vertex: 32 bytes  (vs ~48 bytes uncompressed)
// Skeletal mesh vertex: 64 bytes (vs ~80 bytes uncompressed)
// Mesh cluster: 32 bytes
// -----------------------------------------------------------------------------

namespace tucano::renderer {

struct StaticMeshVertex {
	uint16_t compressedPositionX = 0;
	uint16_t compressedPositionY = 0;
	uint16_t compressedPositionZ = 0;
	int16_t  compressedNormalX = 0;
	int16_t  compressedNormalY = 0;
	int16_t  compressedNormalZ = 0;
	glm::vec2 uv0{0.0f, 0.0f};
	glm::vec2 uv1{0.0f, 0.0f};
	uint32_t packedColor = 0xFFFFFFFF; // RGBA8
};
static_assert(sizeof(StaticMeshVertex) == 32, "StaticMeshVertex must be 32 bytes");

struct SkeletalMeshVertex {
	StaticMeshVertex vertex;  // 32 bytes
	glm::ivec4 boneIndices{0, 0, 0, 0};
	glm::vec4  boneWeights{0.0f, 0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(SkeletalMeshVertex) == 64, "SkeletalMeshVertex must be 64 bytes");

struct MeshCluster {
	uint16_t boundingSphere[4] = {};   // xyz + padding
	uint32_t vertexOffset = 0;
	uint32_t triangleOffset = 0;
	int32_t  anchorX : 24 = 0;
	uint32_t numVertices : 8 = 0;       // max 64 (6 bits)
	int32_t  anchorY : 24 = 0;
	uint32_t numTriangles : 8 = 0;      // max 124 (7 bits)
	int32_t  anchorZ : 24 = 0;
	int32_t  sharedExponent : 8 = 0;
	float    boundingSphereRadius = 0.0f;
};
static_assert(sizeof(MeshCluster) == 32, "MeshCluster must be 32 bytes");

// Max constraints matching Esoterica design
static constexpr uint32_t kMaxVerticesPerCluster = 64;
static constexpr uint32_t kMaxTrianglesPerCluster = 124;

} // namespace tucano::renderer
