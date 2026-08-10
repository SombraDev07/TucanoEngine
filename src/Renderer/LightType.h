#pragma once

// One enum, in its own header, so that authoring code can name a light's kind without pulling in
// the renderer.
//
// `LightComponent` (ECS) and `Light` (renderer) have to agree on this, and they are on opposite
// sides of a layer boundary. Including Scene.h from an ECS header to reach it would drag Mesh,
// Material and Camera into every translation unit that merely wants to declare a light — and
// declaring the enum twice would mean two lists that quietly disagree the first time one grows.

#include "Core/TypeSystem/ReflectionMacros.h"

#include <cstdint>

namespace tucano {

enum class TUCANO_ENUM() LightType : uint32_t {
	Directional = 0,
	Point = 1,
	Spot = 2
};

} // namespace tucano
