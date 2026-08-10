#include "ECS/AuthoringComponents.h"

#include "ECS/Components.h"
#include "ECS/World.h"

namespace tucano::ecs {

uint32_t kCompName = kInvalidEntity;
uint32_t kCompMesh = kInvalidEntity;
uint32_t kCompLight = kInvalidEntity;

void registerAuthoringComponents() {
	// registerComponent<T> is itself idempotent (it checks ComponentTag<T>::id), so calling this
	// twice is free rather than a duplicate-registration bug.
	kCompName = registerComponent<NameComponent>("name");
	kCompMesh = registerComponent<MeshComponent>("mesh_ref");
	kCompLight = registerComponent<LightComponent>("light");
}

namespace {

// One pair of thunks per component, so the table can add and remove without the caller naming the
// type. `world.add<T>()` default-constructs; the id-based overload on EntityManager only zeroes.
template <typename T>
void* addComponent(World& world, Entity entity) {
	return world.add<T>(entity);
}

template <typename T>
void removeComponent(World& world, Entity entity) {
	world.remove<T>(entity);
}

} // namespace

const AuthoringComponentInfo* authoringComponents() {
	static const AuthoringComponentInfo kComponents[] = {
	    {"name", "Entity", "NameComponent", &kCompName, &addComponent<NameComponent>,
	     &removeComponent<NameComponent>, /*essential=*/true},
	    {"transform", "Transform", "TransformComponent", &kCompTransform,
	     &addComponent<TransformComponent>, &removeComponent<TransformComponent>,
	     /*essential=*/true},
	    {"mesh_ref", "Mesh", "MeshComponent", &kCompMesh, &addComponent<MeshComponent>,
	     &removeComponent<MeshComponent>, /*essential=*/false},
	    {"light", "Light", "LightComponent", &kCompLight, &addComponent<LightComponent>,
	     &removeComponent<LightComponent>, /*essential=*/false},
	};
	return kComponents;
}

size_t authoringComponentCount() { return 4; }

} // namespace tucano::ecs
