#include "ECS/RenderSync.h"

#include "AssetPipeline/AssetResolver.h"
#include "ECS/AuthoringComponents.h"
#include "ECS/Components.h"
#include "ECS/QueryManager.h"
#include "ECS/World.h"
#include "Renderer/Material.h"
#include "Renderer/Scene.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tucano::ecs {

void syncMeshComponentsToScene(World& world, tucano::Scene& scene, AssetResolver& resolver,
                               RenderSyncState& state) {
	// "mesh" is RenderObjectComponent (where in the scene), "mesh_ref" is MeshComponent (what to
	// show). Both read-write: the sync writes back the GUID it just applied.
	const QueryDesc desc{{"mesh", "mesh_ref"}, {}, {}, {}};
	const QueryId q = world.queries().registerQuery(desc);
	world.queries().performQueryChunks(q, [&](ComponentAccessor& acc) {
		auto* renders = acc.column<RenderObjectComponent>();
		auto* meshes = acc.column<MeshComponent>();
		for (uint32_t i = 0; i < acc.count(); ++i) {
			const RenderObjectHandle handle = renders[i].handle;
			RenderObject* resolved = scene.resolve(handle);
			if (resolved == nullptr) {
				continue;
			}
			RenderObject& object = *resolved;

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
				if (state.originals.find(handle) == state.originals.end()) {
					state.originals.emplace(handle, object.materials);
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
			if (const auto found = state.originals.find(handle); found != state.originals.end()) {
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

size_t spawnMeshObjects(World& world, tucano::Scene& scene, AssetResolver& resolver,
                        RenderSyncState& state) {
	if (kCompMesh == kInvalidEntity || kCompRenderObject == kInvalidEntity) return 0;

	// Collected during the query, acted on after it — see the note in the header. Entity ids rather
	// than pointers, because the archetype migration that follows invalidates every pointer the
	// query handed out.
	std::vector<std::pair<Entity, asset::AssetGuid>> pending;

	const auto wanted = [&](const asset::AssetGuid& guid) {
		if (!guid.valid()) return false;
		// A GUID that already failed is not retried until the project is re-scanned: loading a mesh
		// reads a file and uploads buffers, and doing that every frame for something that is not
		// going to appear would cost the frame rate to no purpose.
		return state.failedMeshes.count(guid.hi ^ guid.lo) == 0;
	};

	// Entities with no render object at all — the ordinary case, an entity just given a mesh.
	const QueryDesc fresh{{}, {"mesh_ref"}, {}, {"mesh"}};
	world.queries().performQueryChunks(world.queries().registerQuery(fresh),
	                                   [&](ComponentAccessor& acc) {
		const auto* meshes = acc.column<MeshComponent>();
		for (uint32_t i = 0; i < acc.count(); ++i) {
			if (!wanted(meshes[i].mesh)) continue;
			pending.emplace_back(acc.entities()[i], meshes[i].mesh);
		}
	});

	// Entities whose object is *gone*: removed by a streaming cell unload, a terrain tile release,
	// the Outliner's delete, or a scene reload. Without this pass an entity that lost its object
	// would keep its component, so the creation path would never fire again and the thing would be
	// invisible for the rest of the session with no way back — silently, because the syncs merely
	// `continue` on a handle that does not resolve.
	const QueryDesc orphaned{{}, {"mesh", "mesh_ref"}, {}, {}};
	world.queries().performQueryChunks(world.queries().registerQuery(orphaned),
	                                   [&](ComponentAccessor& acc) {
		const auto* renders = acc.column<RenderObjectComponent>();
		const auto* meshes = acc.column<MeshComponent>();
		for (uint32_t i = 0; i < acc.count(); ++i) {
			if (scene.resolve(renders[i].handle) != nullptr) continue;
			if (!wanted(meshes[i].mesh)) continue;
			pending.emplace_back(acc.entities()[i], meshes[i].mesh);
		}
	});

	// Which of the objects this sync created still have an entity pointing at them. Everything else
	// it made is abandoned, and its slot goes back to the scene's free list to be handed out again.
	//
	// The counterweight that keeps Play → Stop from doubling the scene: that path destroys every
	// entity and rebuilds it from JSON, and `RenderObjectComponent` is runtime state no file carries,
	// so the rebuilt entities come back with no link to their objects.
	std::unordered_set<RenderObjectHandle> referenced;
	const QueryDesc linked{{}, {"mesh"}, {}, {}};
	world.queries().performQueryChunks(world.queries().registerQuery(linked),
	                                   [&](ComponentAccessor& acc) {
		const auto* renders = acc.column<RenderObjectComponent>();
		for (uint32_t i = 0; i < acc.count(); ++i) referenced.insert(renders[i].handle);
	});

	// Freed rather than hidden-and-remembered, which is what this had to do while removal meant
	// erasing: the scene now owns the recycling, so releasing the slot is the whole operation.
	std::vector<RenderObjectHandle> stillOurs;
	stillOurs.reserve(state.spawned.size());
	for (const RenderObjectHandle handle : state.spawned) {
		if (scene.resolve(handle) == nullptr) continue; // already gone; forget it
		if (referenced.count(handle) != 0) {
			stillOurs.push_back(handle);
			continue;
		}
		scene.removeObject(handle);
	}
	state.spawned = std::move(stillOurs);

	size_t created = 0;
	for (const auto& [entity, guid] : pending) {
		if (!world.alive(entity)) continue;

		// The asset carries the material it was cooked with, so a mesh dropped into a scene arrives
		// looking like it did in the source rather than flat white.
		std::shared_ptr<Material> cooked;
		std::string error;
		std::shared_ptr<Mesh> mesh = resolver.mesh(guid, &error, &cooked);
		if (mesh == nullptr) {
			state.failedMeshes.insert(guid.hi ^ guid.lo);
			continue;
		}

		RenderObject object;
		object.mesh = std::move(mesh);
		if (cooked != nullptr) object.materials.push_back(std::move(cooked));
		if (const auto* named = world.get<NameComponent>(entity)) {
			object.name = named->name.c_str();
		}
		// Placed by the transform sync on the same frame; until then it sits at the origin rather
		// than at an uninitialised matrix.
		if (const auto* t = world.get<TransformComponent>(entity)) {
			object.worldMatrix = glm::translate(glm::mat4(1.0f), t->position) *
			                     glm::mat4_cast(t->rotation) *
			                     glm::scale(glm::mat4(1.0f), t->scale);
		}

		// The scene decides whether this reuses a freed slot or grows the vector.
		const RenderObjectHandle handle = scene.addObject(std::move(object));
		state.spawned.push_back(handle);

		// `add` on an entity that already has the component returns the existing one, which is
		// exactly right for the re-created case: the same component is repointed at the new object.
		auto* render = world.add<RenderObjectComponent>(entity);
		render->handle = handle;
		render->appliedMesh = guid;
		++created;
	}
	return created;
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
