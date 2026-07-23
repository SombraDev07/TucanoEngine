#pragma once

#include "Renderer/Texture.h"

#include <memory>

namespace tucano {

// Procedural development textures — the grid/checker look used to block out and test levels.
// Generated rather than shipped as files so the engine always has a usable default material with
// no asset dependency, and so the pattern stays crisp at any resolution.
namespace devtex {

// Dark neutral checkerboard with a thin white grid, a border highlight and a "1 m" scale label.
// One UV tile covers one world unit on a unit primitive, so the label is accurate there.
std::shared_ptr<Texture> checkerAlbedo(rhi::Device& device, uint32_t size = 512,
                                       uint32_t cells = 8);

// Same pattern without the label or border: a ground plane tiles its UVs many times over, so a
// fixed metre marker printed on it would be wrong.
std::shared_ptr<Texture> floorAlbedo(rhi::Device& device, uint32_t size = 512,
                                     uint32_t cells = 8);

// Matching normal map with a slight bevel at the cell borders, so lighting picks up the grid.
std::shared_ptr<Texture> checkerNormal(rhi::Device& device, uint32_t size = 512,
                                       uint32_t cells = 8);

// Cached singletons for the editor's default material. Built on first use.
std::shared_ptr<Texture> defaultAlbedo(rhi::Device& device);
std::shared_ptr<Texture> defaultFloor(rhi::Device& device);
std::shared_ptr<Texture> defaultNormal(rhi::Device& device);

} // namespace devtex
} // namespace tucano
