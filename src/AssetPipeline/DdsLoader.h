#pragma once

#include "AssetPipeline/ImageLoader.h"

#include <string>

namespace tucano {

// Loads DX10 DDS (BC4/BC5/RGBA8) mip0 into RGBA8. Throws on failure.
ImageData loadDdsRGBA8(const std::string& path);

} // namespace tucano
