#include "Editor/ViewportInteraction.h"

#include "Editor/EditorContext.h"
#include "Editor/SceneCommands.h"
#include "ECS/EntityManager.h"
#include "ECS/World.h"
#include "Renderer/Camera.h"
#include "Renderer/Mesh.h"
#include "Renderer/Scene.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tucano::editor {
namespace {

// Below this a scale axis is degenerate and normalising it would divide by ~0. The object is
// invisible at that size anyway; what matters is that the rotation comes back sane instead of NaN,
// because a NaN quaternion written into the transform makes the object disappear *permanently*.
constexpr float kMinAxisLength = 1e-6f;

ImGuizmo::OPERATION toImGuizmo(GizmoOperation operation) {
	switch (operation) {
		case GizmoOperation::Rotate: return ImGuizmo::ROTATE;
		case GizmoOperation::Scale: return ImGuizmo::SCALE;
		case GizmoOperation::Translate: break;
	}
	return ImGuizmo::TRANSLATE;
}

} // namespace

bool rayHitsAabb(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& aabbMin,
                 const glm::vec3& aabbMax, float& tHit) {
	float tMin = 0.0f;
	float tMax = std::numeric_limits<float>::max();

	for (int axis = 0; axis < 3; ++axis) {
		if (std::abs(dir[axis]) < kMinAxisLength) {
			// Parallel to this pair of planes: a miss unless the origin is already between them.
			//
			// Written out rather than left to the infinities. Dividing by ~0 gives ±inf, and when the
			// origin sits exactly on a plane the product is 0 * inf = NaN — which this loop happens to
			// absorb, because `std::max(tMin, t1)` and `std::min(tMax, t2)` return their *first*
			// argument when the comparison is false, and every comparison against NaN is false.
			// Verified by mutation: dropping this branch does not change a single gate result. It
			// stays because the survival of that NaN depends on the argument order two lines below,
			// and a later tidy-up that swaps them would turn every grazing ray into a miss.
			if (origin[axis] < aabbMin[axis] || origin[axis] > aabbMax[axis]) return false;
			continue;
		}
		const float inv = 1.0f / dir[axis];
		float t1 = (aabbMin[axis] - origin[axis]) * inv;
		float t2 = (aabbMax[axis] - origin[axis]) * inv;
		if (t1 > t2) std::swap(t1, t2);
		tMin = std::max(tMin, t1);
		tMax = std::min(tMax, t2);
		if (tMin > tMax) return false;
	}

	tHit = tMin;
	return true;
}

int pickSceneObject(const Scene& scene, const glm::vec3& origin, const glm::vec3& dir) {
	int best = -1;
	float bestT = std::numeric_limits<float>::max();

	for (size_t i = 0; i < scene.objects.size(); ++i) {
		const RenderObject& object = scene.objects[i];
		// Hidden objects are skipped by every render pass, so clicking where one would have been
		// must not select it — the user is looking at what is behind it.
		if (!object.visible || object.mesh == nullptr) continue;

		const glm::mat4 inverse = glm::inverse(object.worldMatrix);
		const glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(origin, 1.0f));
		// Left unnormalised deliberately: the inverse world matrix carries the object's scale, so
		// `t` comes back in world units and objects of different sizes are ordered correctly.
		const glm::vec3 localDir = glm::vec3(inverse * glm::vec4(dir, 0.0f));

		for (const SubMesh& sub : object.mesh->submeshes()) {
			float t = 0.0f;
			if (!rayHitsAabb(localOrigin, localDir, sub.aabbMin, sub.aabbMax, t)) continue;
			if (t < bestT) {
				bestT = t;
				best = static_cast<int>(i);
			}
		}
	}
	return best;
}

ecs::Entity entityForSceneObject(ecs::World& world, int sceneIndex) {
	if (sceneIndex < 0) return ecs::kInvalidEntity;
	const uint32_t wanted = static_cast<uint32_t>(sceneIndex);

	// Walked directly rather than through a query: this runs once per click, and registering a query
	// for it would add an archetype mask to the manager for the rest of the session.
	for (ecs::EntityManager::Archetype& archetype : world.entities().archetypes()) {
		for (ecs::EntityManager::Chunk& chunk : archetype.chunks) {
			for (uint32_t i = 0; i < chunk.count; ++i) {
				const ecs::Entity entity = chunk.entities[i];
				const auto* render = world.get<ecs::RenderObjectComponent>(entity);
				if (render != nullptr && render->sceneIndex == wanted) return entity;
			}
		}
	}
	return ecs::kInvalidEntity;
}

void decomposeMatrix(const glm::mat4& matrix, glm::vec3& position, glm::quat& rotation,
                     glm::vec3& scale) {
	position = glm::vec3(matrix[3]);

	glm::vec3 axisX(matrix[0]);
	glm::vec3 axisY(matrix[1]);
	glm::vec3 axisZ(matrix[2]);
	scale = {glm::length(axisX), glm::length(axisY), glm::length(axisZ)};

	// A negative determinant means the matrix mirrors. A quaternion cannot express that, so the
	// reflection is moved into the scale instead of being dropped — dropping it would flip the
	// object back the moment the value was written and read again.
	const bool mirrored = glm::determinant(glm::mat3(matrix)) < 0.0f;
	if (mirrored) scale.x = -scale.x;

	axisX = scale.x > kMinAxisLength || scale.x < -kMinAxisLength ? axisX / scale.x
	                                                             : glm::vec3(1.0f, 0.0f, 0.0f);
	axisY = scale.y > kMinAxisLength ? axisY / scale.y : glm::vec3(0.0f, 1.0f, 0.0f);
	axisZ = scale.z > kMinAxisLength ? axisZ / scale.z : glm::vec3(0.0f, 0.0f, 1.0f);

	rotation = glm::normalize(glm::quat_cast(glm::mat3(axisX, axisY, axisZ)));
}

glm::mat4 transformMatrix(const ecs::TransformComponent& transform) {
	return glm::translate(glm::mat4(1.0f), transform.position) * glm::mat4_cast(transform.rotation) *
	       glm::scale(glm::mat4(1.0f), transform.scale);
}

bool applyMatrixToTransform(ecs::World& world, ecs::Entity entity, const glm::mat4& matrix) {
	if (!world.alive(entity)) return false;
	auto* transform = world.get<ecs::TransformComponent>(entity);
	if (transform == nullptr) return false;

	decomposeMatrix(matrix, transform->position, transform->rotation, transform->scale);
	transform->prevPosition = transform->position;
	transform->prevRotation = transform->rotation;
	return true;
}

bool manipulateTransform(const glm::mat4& view, const glm::mat4& proj, glm::mat4& model,
                         GizmoOperation operation, GizmoSpace space, float snap, float x, float y,
                         float width, float height, GizmoLayer layer) {
	if (width <= 0.0f || height <= 0.0f) return false;

	// Scale only means anything in the object's own space, and ImGuizmo ignores WORLD for it anyway.
	const ImGuizmo::MODE mode = (space == GizmoSpace::World && operation != GizmoOperation::Scale)
	                                ? ImGuizmo::WORLD
	                                : ImGuizmo::LOCAL;

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(layer == GizmoLayer::WindowDrawList ? ImGui::GetWindowDrawList()
	                                                          : ImGui::GetBackgroundDrawList());
	ImGuizmo::SetRect(x, y, width, height);

	const float snapValues[3] = {snap, snap, snap};
	ImGuizmo::Manipulate(&view[0][0], &proj[0][0], toImGuizmo(operation), mode, &model[0][0], nullptr,
	                     snap > 0.0f ? snapValues : nullptr);
	return ImGuizmo::IsUsing();
}

bool gizmoIsOver() { return ImGuizmo::IsOver(); }

bool drawViewportGizmo(EditorContext& context, ViewportGizmoState& state, float x, float y,
                       float width, float height, bool& outChanged) {
	outChanged = false;
	// Only entities get a gizmo. A `RenderObject` with no entity behind it is streamed or generated
	// — a terrain tile, a vegetation instance — and its `worldMatrix` is rewritten by whatever owns
	// it, so a drag would be undone before the next frame with no explanation.
	if (context.world == nullptr || context.camera == nullptr || !context.hasSelectedEntity()) {
		state.dragging = false;
		return false;
	}
	auto* transform = context.world->get<ecs::TransformComponent>(context.selectedEntity);
	if (transform == nullptr) {
		state.dragging = false;
		return false;
	}

	float snap = 0.0f;
	if (state.snapEnabled) {
		switch (state.operation) {
			case GizmoOperation::Translate: snap = state.translateSnap; break;
			case GizmoOperation::Rotate: snap = state.rotateSnap; break;
			case GizmoOperation::Scale: snap = state.scaleSnap; break;
		}
	}

	glm::mat4 model = transformMatrix(*transform);
	const bool using_ = manipulateTransform(context.camera->view(), context.camera->proj(), model,
	                                        state.operation, state.space, snap, x, y, width, height,
	                                        GizmoLayer::WindowDrawList);

	if (using_) {
		if (!state.dragging) {
			// The gesture just started. What the transform holds right now is what undo has to
			// restore, and it has to be copied before the first write of this frame.
			state.dragging = true;
			state.dragEntity = context.selectedEntity;
			state.dragBefore = *transform;
		}
		if (applyMatrixToTransform(*context.world, context.selectedEntity, model)) outChanged = true;
	} else if (state.dragging) {
		// Released. One undo step for the whole drag, from where the object was to where it landed.
		state.dragging = false;
		pushTransformEdit(*context.world, context.undo, state.dragEntity, state.dragBefore,
		                  state.operation == GizmoOperation::Rotate     ? "Rotate"
		                  : state.operation == GizmoOperation::Scale    ? "Scale"
		                                                               : "Move");
		state.dragEntity = ecs::kInvalidEntity;
	}

	return using_;
}

bool pickAtViewportPosition(EditorContext& context, float localX, float localY, float width,
                            float height) {
	if (context.camera == nullptr || context.scene == nullptr || width <= 0.0f || height <= 0.0f) {
		return false;
	}

	glm::vec3 origin(0.0f);
	glm::vec3 direction(0.0f, 0.0f, 1.0f);
	context.camera->screenToWorldRay(localX, localY, width, height, origin, direction);

	const int picked = pickSceneObject(*context.scene, origin, direction);
	if (picked < 0) {
		// Clicking the sky deselects. Keeping the selection would make it impossible to see the
		// scene without a gizmo in the middle of it.
		context.clearSelection();
		return true;
	}

	context.selectedObject = picked;
	context.selectedEntity =
	    context.world != nullptr ? entityForSceneObject(*context.world, picked) : ecs::kInvalidEntity;
	return true;
}

} // namespace tucano::editor
