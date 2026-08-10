#include "ECS/RenderSync.h"

#include "AssetPipeline/AssetResolver.h"
#include "ECS/AuthoringComponents.h"
#include "ECS/Components.h"
#include "ECS/QueryManager.h"
#include "ECS/World.h"
#include "Renderer/Material.h"
#include "Renderer/Scene.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace tucano::ecs {

void syncMeshComponentsToScene(World& world, tucano::Scene& scene, AssetResolver& resolver,
                               MaterialSyncState& state) {
	// "mesh" is RenderObjectComponent (where in the scene), "mesh_ref" is MeshComponent (what to
	// show). Both read-write: the sync writes back the GUID it just applied.
	const QueryDesc desc{{"mesh", "mesh_ref"}, {}, {}, {}};
	const QueryId q = world.queries().registerQuery(desc);
	world.queries().performQueryChunks(q, [&](ComponentAccessor& acc) {
		auto* renders = acc.column<RenderObjectComponent>();
		auto* meshes = acc.column<MeshComponent>();
		for (uint32_t i = 0; i < acc.count(); ++i) {
			const uint32_t idx = renders[i].sceneIndex;
			if (idx >= scene.objects.size()) {
				continue;
			}
			RenderObject& object = scene.objects[idx];

			// Unconditional, like worldMatrix above it: the entity is the source of truth and the
			// scene is derived from it, so an edit made straight on the RenderObject does not
			// survive the frame.
			object.visible = meshes[i].visible;

			// Everything below only when the assignment actually changed. Resolving every frame
			// would rebuild the material and re-look-up all of its textures for a value that only
			// changes when somebody clicks something.
			if (meshes[i].material == renders[i].appliedMaterial) {
				continue;
			}

			if (meshes[i].material.valid()) {
				std::shared_ptr<Material> material = resolver.material(meshes[i].material);
				if (material == nullptr) {
					// A GUID that does not resolve leaves appliedMaterial alone, so the next frame
					// tries again. That is what makes the material appear the moment its file is
					// written, instead of the assignment having to be redone.
					continue;
				}
				// Remember what was there the first time an override lands. This is the only moment
				// the originals are still in place and still known to be the originals.
				if (state.originals.find(idx) == state.originals.end()) {
					state.originals.emplace(idx, object.materials);
				}
				// Every slot, not just the first: the renderer picks materials[sub.materialIndex],
				// so overriding only slot 0 would leave most of a multi-material mesh untouched
				// while the Inspector claimed the object's material had changed.
				if (object.materials.empty()) {
					object.materials.push_back(material);
				} else {
					for (std::shared_ptr<Material>& slot : object.materials) {
						slot = material;
					}
				}
				renders[i].appliedMaterial = meshes[i].material;
				continue;
			}

			// Cleared: put the imported materials back, all of them.
			if (const auto found = state.originals.find(idx); found != state.originals.end()) {
				object.materials = std::move(found->second);
				state.originals.erase(found);
			}
			renders[i].appliedMaterial = {};
		}
	});
}

glm::quat lightRotationFor(const glm::vec3& direction) {
	const glm::vec3 from(0.0f, -1.0f, 0.0f);
	const glm::vec3 to = glm::normalize(direction);
	const float d = glm::dot(from, to);
	// Already aimed there.
	if (d > 0.999999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	// Exactly opposite: the cross product is zero and gives no axis, so any perpendicular one will
	// do — a light pointing straight up has no meaningful roll to preserve.
	if (d < -0.999999f) {
		glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), from);
		if (glm::length(axis) < 0.001f) axis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), from);
		return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
	}
	const glm::vec3 axis = glm::cross(from, to);
	const float s = std::sqrt((1.0f + d) * 2.0f);
	return glm::normalize(glm::quat(s * 0.5f, axis / s));
}

void syncLightsToScene(World& world, tucano::Scene& scene) {
	// clear() keeps the capacity, so after the first frame this allocates nothing.
	scene.lights.clear();

	// Transform is read-only here and required: a light with no transform has no position, and
	// placing it at the origin would be an invention rather than a default.
	const QueryDesc desc{{}, {"transform", "light"}, {}, {}};
	const QueryId q = world.queries().registerQuery(desc);
	world.queries().performQueryChunks(q, [&](ComponentAccessor& acc) {
		const auto* transforms = acc.column<TransformComponent>();
		const auto* lights = acc.column<LightComponent>();
		for (uint32_t i = 0; i < acc.count(); ++i) {
			Light light;
			light.type = lights[i].type;
			light.position = transforms[i].position;
			// Down the entity's local -Y, so rotating the entity aims the light. Matches the default
			// `Light::direction` of {0,-1,0}, which is what an unrotated spot pointed at before.
			light.direction = glm::normalize(transforms[i].rotation * glm::vec3(0.0f, -1.0f, 0.0f));
			light.color = lights[i].color;
			light.intensity = lights[i].intensity;
			light.range = lights[i].range;
			light.innerCone = lights[i].innerCone;
			light.outerCone = lights[i].outerCone;
			light.castShadows = lights[i].castShadows;
			scene.lights.push_back(light);
		}
	});
}

} // namespace tucano::ecs
