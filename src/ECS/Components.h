#pragma once

#include "Renderer/Scene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace tucano::ecs {

struct TransformComponent {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f};
};

struct PhysicsBodyComponent {
  JPH::BodyID joltBodyId;
  bool kinematic = false;
};

struct RenderObjectComponent {
  size_t sceneIndex = 0;
};

} // namespace tucano::ecs
