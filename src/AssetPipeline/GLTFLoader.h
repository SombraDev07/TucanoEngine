#pragma once

#include "Renderer/Scene.h"
#include "RHI/RHI.h"

#include <string>

namespace tucano {

bool loadGLTFScene(rhi::Device& device, const std::string& path, Scene& outScene);

// Import a GLTF file and convert all meshes/textures/materials to .tuasset format.
// Output goes to `outputDir/<filename>/`. Returns number of assets created.
int importGLTFAsTuasset(const std::string& path, const std::string& outputDir);

} // namespace tucano
