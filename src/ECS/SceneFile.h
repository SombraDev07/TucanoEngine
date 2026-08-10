#pragma once

// `.tuscene` — what the editor saves and opens.
//
// C-03 of the roadmap, and step 9 of the Definition of Done: *save, close, open, everything is
// back*. Until this existed, every value tuned in a panel died with the process.
//
// Shape of the file:
//
//   {
//     "format": "tuscene", "version": 1,
//     "entities": [ { "name": {...}, "transform": {...}, "light": {...} } ],
//     "environment": { "WaterParams": {...}, "FogParams": {...}, ... }
//   }
//
// The *container* — the entity list and which components each one has — is written by this file.
// Each component's *fields* come from reflection, so a component that grows a property is saved
// and loaded with no change here. That split is deliberate: type-erased reflection over
// `std::vector<T>` does not exist yet (CP-24), and inventing it to write one array would be a lot
// of machinery for no extra correctness.
//
// Components are keyed by their **registered ECS name** ("transform", "light"), not by their C++
// type name, because that is the name the template system already uses and the one that has to
// stay stable in files.
//
// Unknown component keys are skipped rather than failing the load: a scene saved by a build that
// has a component this one does not should still open, minus what it cannot represent.

#include <string>
#include <string_view>

#include "ECS/ComponentTypes.h"

namespace tucano {
struct RendererSettings;
struct WaterParams;
struct FogParams;
struct CloudParams;
struct RainParams;
} // namespace tucano

namespace tucano::ecs {

class World;

// Everything in a scene that is not an entity. Held by pointer so a caller that has no renderer
// (a test, a headless cook) can still save and load the entity half.
struct SceneEnvironment {
	WaterParams* water = nullptr;
	FogParams* fog = nullptr;
	CloudParams* clouds = nullptr;
	RainParams* rain = nullptr;
};

// Serialises every live entity that carries at least one authoring component. Entities that only
// hold runtime state (a Jolt body id, an index into the render scene) are not written — they are
// rebuilt on load, and writing them would bake this run's addresses into a file.
std::string sceneToJson(World& world, const SceneEnvironment& environment = {});

// Replaces the contents of `world`. Returns false only when the text is not a valid scene; a
// component that fails to read is reported through `err` and skipped, so one bad entity does not
// cost the level.
bool sceneFromJson(std::string_view json, World& world, const SceneEnvironment& environment = {},
                   std::string* err = nullptr);

// ── One entity ──────────────────────────────────────────────────────────────
//
// The same component table the whole-scene path uses, applied to a single entity. This is what
// duplicate and undo-of-delete are built from: an entity is defined by its components, so a copy
// of those components *is* a copy of the entity.
//
// `entityFromJson` always creates a **new** entity — EntityManager assigns ids and has no "create
// with this id", so an entity restored by undo comes back with a different id. Anything holding
// the old id (a selection, a parent reference) has to be told; the editor clears the selection.

std::string entityToJson(World& world, Entity entity);
Entity entityFromJson(World& world, std::string_view json, std::string* err = nullptr);

bool saveScene(const std::string& path, World& world, const SceneEnvironment& environment = {},
               std::string* err = nullptr);
bool loadScene(const std::string& path, World& world, const SceneEnvironment& environment = {},
               std::string* err = nullptr);

} // namespace tucano::ecs
