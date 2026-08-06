#pragma once

// Single entry point for source-model → .tuasset conversion, plus the reverse path that turns a
// cooked .tuasset back into a renderable Mesh. Callers (editor C ABI, tools) should use these
// rather than the format-specific importers so a new source format is one switch case, not a new
// call site everywhere.

#include "Renderer/Material.h"
#include "Renderer/Mesh.h"
#include "RHI/RHI.h"

#include <memory>
#include <string>
#include <vector>

namespace tucano {

/// Converts `path` (.gltf/.glb/.fbx) into one .tuasset per mesh primitive under
/// `outputDir/<sourceStem>/`. Returns the number of assets written (0 = nothing imported).
/// Unknown extensions return 0 rather than being fed to the wrong parser.
int importModelAsTuasset(const std::string& path, const std::string& outputDir);

/// True when `path` has an extension importModelAsTuasset knows how to read.
bool isImportableModel(const std::string& path);

/// Reads a cooked .tuasset mesh back into GPU buffers. Handles both the Draco (DRCV) and the
/// uncompressed (VERT/INDX) chunk layouts. Returns null and fills `error` on failure.
///
/// When `outMaterial` is given and the asset carries a MATL chunk, it is filled with the material
/// the source model had — factors plus any textures, reloaded from the paths recorded at import.
/// Left untouched when the asset has no material, so callers can supply their own default.
std::shared_ptr<Mesh> loadTuassetMesh(rhi::Device& device, const std::string& path,
                                      std::string* error = nullptr,
                                      std::shared_ptr<Material>* outMaterial = nullptr);

/// MATL chunk payload: a MaterialAssetData header followed by four length-prefixed texture paths
/// (albedo, normal, metallicRoughness, emissive), each possibly empty. Absolute paths, recorded at
/// import time — the format carries no texture pixels of its own yet, so a moved source folder
/// costs the textures, not the mesh.
std::vector<uint8_t> encodeMaterialChunk(const glm::vec4& baseColor, const glm::vec3& emissive,
                                         float metallic, float roughness, float alphaCutoff,
                                         bool alphaMask, const std::string& albedoPath,
                                         const std::string& normalPath, const std::string& ormPath,
                                         const std::string& emissivePath);

} // namespace tucano
