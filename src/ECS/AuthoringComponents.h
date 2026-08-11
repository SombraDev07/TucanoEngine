#pragma once

#include "Renderer/LightType.h"

// Components a person authors in the editor, as opposed to the ones the simulation keeps for
// itself (`PhysicsBodyComponent` holds a Jolt body id; `RenderObjectComponent` holds a handle into
// the render scene — neither means anything in a file).
//
// C-02a of the roadmap. The decision behind it (section 8): the entity is the unit of authoring
// *and* of runtime, and `Scene` is render data derived from it. That is the shape Unity, Unreal,
// O3DE and Esoterica all use, and the engine had already committed to it —
// `RenderObjectComponent::handle` plus `syncTransformsToScene` were already the bridge. Only
// the editor had not followed.
//
// Every component here is reflected, which buys three things at once: it appears in the Inspector
// with no UI written for it, it serialises into a `.tuscene`, and it is what Play mode snapshots.
//
// Strings are `FixedString`, not `std::string`: `EntityManager` moves components between archetypes
// with memcpy, and `ComponentTypes.h` enforces that with a static_assert. See Core/FixedString.h.
//
// Position and orientation are **not** repeated per component. A light's place in the world is its
// `TransformComponent`, the same as a mesh's. Duplicating it is how the two silently disagree.

#include "Core/AssetGuid.h"
#include "Core/FixedString.h"
#include "Core/TypeSystem/ReflectionMacros.h"
#include "ECS/ComponentTypes.h"

#include <glm/glm.hpp>

namespace tucano::ecs {

// What the entity is called in the Outliner. Separate from the transform so that renaming is not
// a change to the transform's undo history.
struct TUCANO_TYPE() NameComponent {
	TUCANO_FIELD(.label = "Name", .category = "Entity")
	NameString name{"Entity"};
};

// A mesh to draw, referenced by **identity** rather than by path. A path breaks the moment the
// file is renamed; the GUID comes from the asset's `.tumeta` sidecar and travels with it. The
// loaded `Mesh` is resolved through the AssetRegistry at load time and lives in the render scene,
// not here.
struct TUCANO_TYPE() MeshComponent {
	TUCANO_FIELD(.label = "Mesh", .tooltip = "Asset drawn at this entity's transform",
	             .category = "Mesh", .assetKind = "mesh")
	asset::AssetGuid mesh;

	// One slot for now. A mesh with several primitives wants a list, and reflection has no dynamic
	// array yet (CP-24) — so this is the override for the whole mesh, and per-primitive assignment
	// waits for that. Invalid means "use whatever the mesh asset came with".
	TUCANO_FIELD(.label = "Material", .tooltip = "Overrides the material the mesh was imported with",
	             .category = "Mesh", .assetKind = "material")
	asset::AssetGuid material;

	TUCANO_FIELD(.label = "Visible",
	             .tooltip = "Hidden entities are skipped by every pass, including shadows and GI",
	             .category = "Mesh")
	bool visible = true;
};

// A light. Mirrors `tucano::Light` minus `position` and `direction`, which come from the transform.
struct TUCANO_TYPE() LightComponent {
	// Was an int32 with a 0..2 range until enum reflection existed, which meant the Inspector asked
	// people to remember that 2 means Spot, and scene files stored a number that would change
	// meaning the day a type was inserted in the middle.
	TUCANO_FIELD(.label = "Type", .category = "Light")
	LightType type = LightType::Point;

	TUCANO_FIELD(Color, .label = "Color", .category = "Light")
	glm::vec3 color{1.0f};

	TUCANO_FIELD(.label = "Intensity", .category = "Light", .minValue = 0.0f, .maxValue = 100.0f,
	             .step = 0.1f)
	float intensity = 1.0f;

	TUCANO_FIELD(.label = "Range", .tooltip = "Metres at which the light stops contributing; point and spot only",
	             .category = "Light", .minValue = 0.0f, .maxValue = 500.0f, .step = 0.5f)
	float range = 10.0f;

	TUCANO_FIELD(.label = "Inner cone", .tooltip = "Spot only. Degrees of the fully lit centre",
	             .category = "Spot", .minValue = 0.0f, .maxValue = 90.0f, .step = 0.5f)
	float innerCone = 0.0f;

	TUCANO_FIELD(.label = "Outer cone", .tooltip = "Spot only. Degrees at which the cone reaches zero",
	             .category = "Spot", .minValue = 0.0f, .maxValue = 90.0f, .step = 0.5f)
	float outerCone = 30.0f;

	TUCANO_FIELD(.label = "Cast shadows", .category = "Light")
	bool castShadows = true;
};

// Ids of the authoring components, filled by registerAuthoringComponents().
extern uint32_t kCompName;
extern uint32_t kCompMesh;
extern uint32_t kCompLight;

// Idempotent, like registerCoreComponents(). Called by World's constructor path so that no caller
// has to remember — the lesson from CP-20b and CP-21.
void registerAuthoringComponents();

class World;

// The authorable components, in one place (C-07).
//
// There used to be two copies of this list — one in SceneFile.cpp deciding what a scene writes, one
// in InspectorPanel.cpp deciding what the Inspector shows — and "Add Component" would have been a
// third. Three lists of the same four things is three chances for them to disagree, and the failure
// is quiet: a component that saves but cannot be seen, or vice versa.
//
// Runtime-only components are absent on purpose. `PhysicsBodyComponent` holds a Jolt body id and
// `RenderObjectComponent` a handle into the render scene; neither means anything in a file, and
// neither is something a person authors.
struct AuthoringComponentInfo {
	// Key in the scene file. Stable — renaming it breaks every saved scene.
	const char* key;
	// Heading in the Inspector, and the label in the Add Component menu.
	const char* label;
	// Registered reflected type, which is what gives it an editor.
	const char* typeName;
	// ECS id, filled by registerAuthoringComponents().
	uint32_t* id;
	// Adds the component **default-constructed**, not zeroed. `EntityManager::add(entity, id)`
	// returns zeroed memory, and a zeroed LightComponent is a black light with zero range — it
	// would look like Add Component was broken.
	void* (*add)(World&, Entity);
	void (*remove)(World&, Entity);
	// Cannot be removed. A transform is what "where is it" means, and a name is how the Outliner
	// refers to the thing at all; removing either leaves an entity nobody can find or place.
	bool essential;
};

// Four entries today; the count is a function so a caller cannot get it out of step with the array.
const AuthoringComponentInfo* authoringComponents();
size_t authoringComponentCount();

} // namespace tucano::ecs
