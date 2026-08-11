#pragma once

// The other half of the ECS → Scene bridge.
//
// `syncTransformsToScene` (in PhysicsSync.h) already pushes *where* an entity is into the render
// scene. This pushes *what it looks like*: the material override and the visibility flag that
// `MeshComponent` carries.
//
// Until now both of those were values the Inspector wrote and nothing read. Assigning a material
// changed a file and not a pixel; ticking "Visible" off did nothing at all. That is the worst kind
// of editor bug, because the UI looks like it worked.
//
// Named for the component rather than for materials alone: material and visibility come from the
// same component, are read in the same pass, and splitting them would mean walking the same
// entities twice to save nothing.

#include "Core/AssetGuid.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tucano {
class Scene;
class AssetResolver;
struct Material;
} // namespace tucano

namespace tucano::ecs {

class World;

// What the sync has to remember between frames.
//
// Held by the caller rather than inside the function because it is per-scene state, and a static
// would leak across a New or an Open. It exists for one reason: **clearing an override has to put
// the original material back**, and the original is only knowable at the moment it is first
// replaced. Without this, unassigning would silently leave the overridden material in place — the
// UI lying again, in the opposite direction.
struct RenderSyncState {
	// Scene index → every material slot the mesh was imported with, captured the first time an
	// override replaced them. A whole vector because the renderer indexes materials per submesh
	// (`materials[sub.materialIndex]`) while `MeshComponent` carries a single assignment: one
	// override therefore has to stand in for all of the object's slots, and restoring has to put
	// all of them back. When per-slot assignment exists, this becomes per-slot too.
	std::unordered_map<uint32_t, std::vector<std::shared_ptr<Material>>> originals;

	// GUIDs that failed to load, so a mesh that cannot be read is not retried sixty times a second.
	// Deliberately *not* applied to materials: a material is cheap to re-read and appearing the
	// moment its file is written is worth the cost (CP-35). A mesh is neither.
	//
	// Cleared by `clear()`, which the host calls after re-scanning the project — which is exactly
	// when a previously missing asset might have arrived.
	std::unordered_set<uint64_t> failedMeshes;

	// Scene indices `spawnMeshObjects` created, so an object whose entity is gone can be reused
	// instead of leaked.
	//
	// The counterweight this feature would otherwise lack. **Play → Stop destroys every entity and
	// rebuilds them from JSON** (PlayMode.cpp:64 → SceneFile.cpp:238-240), and
	// `RenderObjectComponent` is runtime state that no file carries — so the rebuilt entities come
	// back with no link to their objects. Without reuse, every Play/Stop cycle would append a
	// duplicate set of the whole scene, doubling draws and shadow work until `Renderer.cpp` runs out
	// of object slots. Deleting an entity leaves the same orphan.
	//
	// Reuse rather than erase, because erasing compacts the array and every other entity's
	// `sceneIndex` would shift under it.
	std::vector<uint32_t> spawned;

	void clear() {
		originals.clear();
		failedMeshes.clear();
		spawned.clear();
	}
};

// Kept as the old name so existing callers still compile; the struct outgrew "material" when it
// started remembering failed meshes too.
using MaterialSyncState = RenderSyncState;

// Applies `MeshComponent`'s material override and visibility to the render scene.
//
// Cheap to call every frame: the resolved GUID is remembered on `RenderObjectComponent`, so a
// material is only rebuilt when the assignment actually changes. Resolving per frame would reload
// textures per frame.
void syncMeshComponentsToScene(World& world, tucano::Scene& scene, AssetResolver& resolver,
                               RenderSyncState& state);

// Gives geometry to entities that reference a mesh but have nothing in the render scene yet (C-08).
//
// This is what makes "place an object" true. `Add entity` plus a `MeshComponent` used to produce a
// reference that resolved to a file and to no pixels, because the mesh sync matches an entity to a
// `RenderObject` that already exists — and an entity created in the editor has none.
//
// **Separate from `syncMeshComponentsToScene`, and it has to be.** That one runs inside
// `World::queries().performQueryChunks`, which holds a reference into the archetype vector and
// iterates chunks by index (QueryManager.cpp:112-128). Adding a component there migrates the
// entity to another archetype, which can reallocate that vector and shrink the chunk being walked.
// So the entities are collected during the query and given their component after it returns.
//
// Returns how many objects were created, which is what a caller logs or asserts on.
size_t spawnMeshObjects(World& world, tucano::Scene& scene, AssetResolver& resolver,
                        RenderSyncState& state);

// Rebuilds `scene.lights` from the entities that have a `LightComponent` (D-02).
//
// **Rebuild, not patch.** Meshes are synced through an index (`RenderObjectComponent::sceneIndex`)
// because the render scene owns the mesh resources and an entity only points at one. Lights own
// nothing: a `Light` is nine numbers. So the entities can simply *be* the list, which removes the
// whole class of bugs an index invites — deleting one light would otherwise have to compact the
// array and fix up every other entity's index, and getting that wrong is silent.
//
// **This makes the ECS the owner of lights.** Whoever calls this is declaring that anything else
// appending to `scene.lights` will be discarded on the next frame; a caller with no light entities
// should simply not call it. Position comes from `TransformComponent::position` and direction from
// its rotation, so moving a light entity moves the light — there is no second place to edit.
void syncLightsToScene(World& world, tucano::Scene& scene);

// The rotation that aims a light entity along `direction` — the exact inverse of what
// `syncLightsToScene` does when it reads the rotation back.
//
// Lives here, next to its inverse, because the two only mean anything as a pair: if they disagree,
// every directional and spot light quietly rotates on the first frame and the scene stays lit, just
// wrongly. Written out rather than using `glm::rotation`, which lives in a GTX header that demands
// GLM_ENABLE_EXPERIMENTAL — not a define worth turning on project-wide for six lines of algebra.
glm::quat lightRotationFor(const glm::vec3& direction);

} // namespace tucano::ecs
