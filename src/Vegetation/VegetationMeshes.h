#pragma once

#include "Renderer/Mesh.h"
#include "RHI/RHI.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace tucano::veg {

std::shared_ptr<Mesh> makeProceduralGrassMesh(rhi::Device& device, float width = 0.12f,
                                              float height = 0.55f);

std::shared_ptr<Mesh> makeProceduralPlantMesh(rhi::Device& device, float height = 1.0f);

/// Single-card simplified LOD1 grass.
std::shared_ptr<Mesh> makeProceduralGrassLOD1Mesh(rhi::Device& device, float width = 0.14f,
                                                  float height = 0.5f);

/// Unit quad in XY for camera-facing billboard impostors (LOD2).
std::shared_ptr<Mesh> makeBillboardQuadMesh(rhi::Device& device);

std::shared_ptr<Mesh> loadVegetationMesh(rhi::Device& device, const std::string& path,
                                         std::string* error = nullptr);

/// Impostor atlas cells per row. 4x4 covers the 16 GPU vegetation types with the largest cells a
/// 1024 atlas allows; a 16x16 grid left fifteen of every sixteen rows empty and shrank each plant
/// to an 8-pixel-wide smear. Must stay in sync with InstanceCloudRender::billboardGrid.
inline constexpr uint32_t kImpostorGrid = 4;

/// One vegetation type as the impostor baker sees it. Raw geometry rather than a Mesh so the bake
/// needs no device and can be exercised headless.
struct ImpostorSource {
	const std::vector<glm::vec3>* positions = nullptr; ///< null falls back to a generic silhouette
	const std::vector<uint32_t>* indices = nullptr;
	glm::vec4 baseColor{0.25f, 0.55f, 0.18f, 1.0f};

	/// Fills positions/indices from a mesh's retained CPU geometry.
	static ImpostorSource fromMesh(const Mesh* mesh, const glm::vec4& color);
};

/// Rasterises the yaw-ring impostor atlas into an RGBA8 buffer: one grid cell per type,
/// `viewsPerType` horizontal slices per cell, each slice an orthographic render of that type's
/// real LOD0 geometry at that yaw. Returns atlasSize*atlasSize*4 bytes.
std::vector<uint8_t> bakeImpostorPixels(const std::vector<ImpostorSource>& types,
                                        uint32_t viewsPerType = 8, uint32_t atlasSize = 1024);

/// bakeImpostorPixels uploaded as a texture.
std::shared_ptr<rhi::Texture> bakeImpostorAtlas(rhi::Device& device,
                                                const std::vector<ImpostorSource>& types,
                                                uint32_t viewsPerType = 8,
                                                uint32_t atlasSize = 1024);

} // namespace tucano::veg
