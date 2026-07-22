#pragma once

#include "ECS/ComponentTypes.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace tucano::ecs {

struct TransformComponent {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f};
  // Physics state at the previous fixed step, for render interpolation (smooth motion when the
  // render rate differs from the fixed physics rate). Seeded to the current state on first use.
  glm::vec3 prevPosition{0.0f};
  glm::quat prevRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct PhysicsBodyComponent {
  JPH::BodyID joltBodyId;
  bool kinematic = false;
};

struct RenderObjectComponent {
  uint32_t sceneIndex = 0;
};

// Ids estáveis dos componentes-core (registrados em registerCoreComponents()).
extern uint32_t kCompTransform;
extern uint32_t kCompPhysicsBody;
extern uint32_t kCompRenderObject;

// Registra os componentes-core + os aplicadores de template JSON. Idempotente.
void registerCoreComponents();

} // namespace tucano::ecs
