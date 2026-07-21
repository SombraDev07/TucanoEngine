#pragma once

#include "Renderer/Scene.h"
#include "RHI/RHI.h"

#include <string>

namespace tucano {

bool loadGLTFScene(rhi::Device& device, const std::string& path, Scene& outScene);

} // namespace tucano
