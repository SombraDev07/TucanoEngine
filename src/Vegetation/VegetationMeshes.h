#pragma once

#include "Renderer/Mesh.h"
#include "RHI/RHI.h"

#include <memory>
#include <string>

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

/// CPU-baked yaw-ring impostor atlas (RGBA8). One atlas cell per type.
std::shared_ptr<rhi::Texture> bakeImpostorAtlas(rhi::Device& device, uint32_t typeCount,
                                                uint32_t viewsPerType = 8,
                                                uint32_t atlasSize = 1024);

} // namespace tucano::veg
