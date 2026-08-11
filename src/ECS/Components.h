#pragma once

#include "Core/AssetGuid.h"
#include "Core/TypeSystem/ReflectionMacros.h"
#include "ECS/ComponentTypes.h"
#include "Renderer/RenderObjectHandle.h"

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
  // A handle, not an index (C-09). An index was a promise the render scene could not keep: it is
  // compacted by streaming cell unloads, terrain tile releases and the Outliner's delete, and after
  // any of those every index above the removed one names a different object. The syncs only
  // `continue` on an out-of-range index, so the entity would silently start driving somebody else's
  // geometry. A handle that no longer resolves reads as "gone", and `spawnMeshObjects` gives the
  // entity a new object.
  //
  // Invalid by default rather than 0: a zeroed component used to mean "object number zero", so any
  // path that produced one aimed the entity at the first object in the scene.
  RenderObjectHandle handle = kInvalidRenderObject;

  // Which material override is currently applied to that scene object. Runtime state, never
  // serialised: it exists so the sync can tell "nothing changed" from "not applied yet" and skip
  // rebuilding a material — and therefore reloading its textures — on every frame.
  asset::AssetGuid appliedMaterial;

  // Same, for the geometry. Loading a mesh uploads vertex and index buffers, so doing it because a
  // frame happened would be far worse than re-resolving a material.
  asset::AssetGuid appliedMesh;
};

// Ids estáveis dos componentes-core (registrados em registerCoreComponents()).
extern uint32_t kCompTransform;
extern uint32_t kCompPhysicsBody;
extern uint32_t kCompRenderObject;

// Registra os componentes-core + os aplicadores de template JSON. Idempotente.
void registerCoreComponents();

} // namespace tucano::ecs
