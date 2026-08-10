#pragma once

#include "Core/AssetGuid.h"

#include "Renderer/Scene.h"
#include "RHI/RHI.h"

#include <string>

namespace tucano {

bool loadGLTFScene(rhi::Device& device, const std::string& path, Scene& outScene);

// `sourceGuid` is the identity of the source file, from its `.tumeta` sidecar. Each primitive
// written gets a sub-asset id derived from it, so re-importing after a rename produces the *same*
// ids and every scene that referenced them keeps resolving. An invalid GUID falls back to hashing
// the path, which is the old behaviour and breaks on rename — callers that have a registry should
// always pass one.
int importGLTFAsTuasset(const std::string& path, const std::string& outputDir,
                        const asset::AssetGuid& sourceGuid = {});

} // namespace tucano
