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
//     "environment": { "SkyParams": {...}, "WaterParams": {...}, ..., "hdri": "IBL/x.hdr" }
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

#include <functional>
#include <string>
#include <string_view>

#include "ECS/ComponentTypes.h"

namespace tucano {
struct RendererSettings;
struct WaterParams;
struct FogParams;
struct CloudParams;
struct RainParams;
struct SkyParams;
struct PostFxParams;
} // namespace tucano

namespace tucano::terrain {
struct TerrainGenParams;
} // namespace tucano::terrain

namespace tucano::ecs {

class World;

// Everything in a scene that is not an entity. Held by pointer so a caller that has no renderer
// (a test, a headless cook) can still save and load the entity half.
//
// **`RendererSettings` is deliberately absent.** A scene wants to save what time of day it is and
// how it is graded; it does not want to save whether meshlets are on, how big the shadow map is, or
// whether ray tracing is available — those are properties of the machine drawing the scene. The
// fields that genuinely belonged here were moved out into structs of their own instead (E-01 for
// the sky, E-05 for the clouds, E-04 for the grading), which is why every pointer below names a
// specific block rather than a filtered view of one big one.
struct SceneEnvironment {
	WaterParams* water = nullptr;
	FogParams* fog = nullptr;
	CloudParams* clouds = nullptr;
	RainParams* rain = nullptr;
	SkyParams* sky = nullptr;
	PostFxParams* postFx = nullptr;

	// The HDRI the scene is lit by. Not one of the blocks above because it is a path, and applying
	// one means re-cooking the image-based lighting — which takes real time, can fail, and has to
	// leave the previous lighting in place when it does. So it travels as the value plus the one
	// operation the host knows how to perform on it.
	std::string* hdriPath = nullptr;

	// Applies a path that was just read; false means it could not be used. Called **only when the
	// path differs** from what is loaded, so opening a scene that names the HDRI already in memory
	// costs nothing — and so Play → Stop, which restores this block from a snapshot, does not
	// re-cook the IBL on every stop.
	//
	// On failure the running lighting is kept and `hdriPath` is left alone: a scene naming a missing
	// HDRI opens lit by whatever you had, not black.
	std::function<bool(const std::string&)> applyHdri;

	// The terrain, as the recipe rather than the result (F-01). Nine numbers regenerate the
	// landscape exactly, on any machine, so a scene stores those instead of a heightmap — a few
	// hundred bytes against a few megabytes.
	//
	// Same value-plus-operation shape as the HDRI above, and for the same reason: building a
	// terrain allocates a heightmap, uploads it and rebuilds a collision mesh. `applyTerrain` is
	// called **only when the parameters differ** from what is already built.
	terrain::TerrainGenParams* terrain = nullptr;
	std::function<bool(const terrain::TerrainGenParams&)> applyTerrain;
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
