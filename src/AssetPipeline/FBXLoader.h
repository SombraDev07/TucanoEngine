#pragma once

// OpenFBX-based FBX importer — converts .fbx to .tuasset assets.
// Handles meshes, materials, skeletons, and animations.

#include <string>

namespace tucano {

int importFBXAsTuasset(const std::string& fbxPath, const std::string& outputDir);

} // namespace tucano
