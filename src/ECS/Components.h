#pragma once

#include "Core/AssetGuid.h"
#include "Core/TypeSystem/ReflectionMacros.h"
#include "ECS/ComponentTypes.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace tucano::ecs {

struct TUCANO_TYPE() TransformComponent {
  TUCANO_FIELD(.label = "Position", .tooltip = "World position of the entity", .category = "Transform", .step = 0.1f)
  glm::vec3 position{0.0f};

  TUCANO_FIELD(.label = "Rotation", .tooltip = "Shown and edited as Euler degrees; stored as a quaternion", .category = "Transform", .step = 0.5f)
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

  TUCANO_FIELD(.label = "Scale", .tooltip = "Per-axis scale; 1 is unscaled", .category = "Transform", .step = 0.01f)
  glm::vec3 scale{1.0f};

  // Physics state at the previous fixed step, for render interpolation (smooth motion when the
  // render rate differs from the fixed physics rate). Seeded to the current state on first use.
  // Transient on purpose: it is last frame's state, so writing it to a file would restore a scene
  // mid-interpolation from a run that no longer exists.
  TUCANO_FIELD(.label = "Prev position", .category = "Interpolation", .readOnly = true, .transient = true)
  glm::vec3 prevPosition{0.0f};

  TUCANO_FIELD(.label = "Prev rotation", .category = "Interpolation", .readOnly = true, .transient = true)
  glm::quat prevRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct PhysicsBodyComponent {
  JPH::BodyID joltBodyId;
  bool kinematic = false;
};

struct RenderObjectComponent {
  uint32_t sceneIndex = 0;

  // Which material override is currently applied to that scene object. Runtime state, never
  // serialised: it exists so the sync can tell "nothing changed" from "not applied yet" and skip
  // rebuilding a material — and therefore reloading its textures — on every frame.
  asset::AssetGuid appliedMaterial;
};

// Ids estáveis dos componentes-core (registrados em registerCoreComponents()).
extern uint32_t kCompTransform;
extern uint32_t kCompPhysicsBody;
extern uint32_t kCompRenderObject;

// Registra os componentes-core + os aplicadores de template JSON. Idempotente.
void registerCoreComponents();

} // namespace tucano::ecs
