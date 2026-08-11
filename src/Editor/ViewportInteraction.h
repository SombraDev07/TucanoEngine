#pragma once

// Selecting and moving things *in the world*, rather than in a list.
//
// Closes the last item of step 4 of the Definition of Done ("place the object, move it with a
// gizmo, duplicate, rename"). Placing, duplicating and renaming already worked; moving did not —
// the only way to change a position was to type numbers into the Inspector.
//
// Two halves, both of which used to live inline in a sample that no longer exists
// (`Samples/TestEditor`, commit b6a58db):
//
//   * **Picking** — turn a click into a selection. Written against the object's own space and its
//     mesh bounds rather than a fixed-radius sphere around the origin, which is what the old code
//     did: a sphere of radius 1.5 makes a lamp post unpickable and a coin the size of a car.
//   * **Manipulation** — ImGuizmo over the viewport image, writing back to the entity's
//     `TransformComponent`, which is what the editor authors (C-02).
//
// The maths is separated from the ImGui call on purpose: a ray/AABB test and a matrix decomposition
// are exactly the parts that break silently, and they are the parts a headless gate can exercise.

#include "ECS/ComponentTypes.h"
#include "ECS/Components.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace tucano {
class Scene;
}

namespace tucano::ecs {
class World;
}

namespace tucano::editor {

struct EditorContext;

// ── Picking ──────────────────────────────────────────────────────────────────

// Slab test. `tHit` comes back as the distance along `dir` at which the ray enters the box, and 0
// when the origin is already inside it. `dir` need not be normalised — `tHit` is then in units of
// `dir`, which is what makes testing in object space give world-comparable distances.
//
// Nothing behind the origin counts: `tMin` starts at 0, so a box the camera has already passed is a
// miss rather than a hit at a negative distance.
bool rayHitsAabb(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& aabbMin,
                 const glm::vec3& aabbMax, float& tHit);

// The nearest visible object the ray enters, or -1.
//
// The ray is taken into each object's own space instead of bringing its bounds out into the world:
// the world-space AABB of a rotated box is bigger than the box, sometimes by a lot, and objects
// would be selectable from where they visibly are not.
int pickSceneObject(const Scene& scene, const glm::vec3& origin, const glm::vec3& dir);

// The entity that owns a render object — the inverse of `RenderObjectComponent::handle`.
// `kInvalidEntity` when nothing claims it, which is what a streamed cell or a terrain tile is.
ecs::Entity entityForSceneObject(ecs::World& world, RenderObjectHandle handle);

// ── Transform maths ──────────────────────────────────────────────────────────

// Splits a world matrix back into the three fields `TransformComponent` stores. A mirrored matrix
// (negative determinant) comes back with a negative X scale, because a quaternion cannot express a
// reflection and silently dropping it would flip the object the next time it was written back.
void decomposeMatrix(const glm::mat4& matrix, glm::vec3& position, glm::quat& rotation,
                     glm::vec3& scale);

// Writes a manipulated matrix onto the entity's transform. False when the entity has no transform.
//
// `prevPosition`/`prevRotation` follow the new value rather than staying behind: they exist for
// render interpolation between fixed steps, and leaving them means a host that interpolates would
// draw the object sliding in from where it used to be. Dragging a gizmo is a teleport, not motion.
bool applyMatrixToTransform(ecs::World& world, ecs::Entity entity, const glm::mat4& matrix);

// The matrix the gizmo is drawn at.
glm::mat4 transformMatrix(const ecs::TransformComponent& transform);

// ── Gizmo ────────────────────────────────────────────────────────────────────

enum class GizmoOperation : uint8_t { Translate, Rotate, Scale };
enum class GizmoSpace : uint8_t { World, Local };

// Where ImGuizmo draws. A panel wants its own window's list so the gizmo is clipped to the viewport
// image; the older whole-window entry point (`DebugUI::drawTransformGizmo`) wants the background,
// because it has no window to live in.
enum class GizmoLayer : uint8_t { WindowDrawList, BackgroundDrawList };

// What the viewport remembers between frames.
struct ViewportGizmoState {
	GizmoOperation operation = GizmoOperation::Translate;
	GizmoSpace space = GizmoSpace::World;

	bool snapEnabled = false;
	float translateSnap = 0.5f;  // metres
	float rotateSnap = 15.0f;    // degrees
	float scaleSnap = 0.1f;

	// A drag writes the transform every frame so the object follows the mouse, but it is **one**
	// edit: the value when the gesture started is kept here and the undo step is pushed on release.
	// Recording per frame instead would rely on the undo stack's coalescing to hide sixty steps.
	bool dragging = false;
	ecs::Entity dragEntity = ecs::kInvalidEntity;
	ecs::TransformComponent dragBefore;
};

// The ImGuizmo call itself, in one place. Returns true while a handle is held.
//
// `x`/`y` are the top-left of the viewport **in screen coordinates**, not window coordinates:
// ImGuizmo hit-tests against `io.MousePos`, which is in screen space, so passing (0,0) for a panel
// that is not at the top-left of the window puts the handles where the mouse is not.
bool manipulateTransform(const glm::mat4& view, const glm::mat4& proj, glm::mat4& model,
                         GizmoOperation operation, GizmoSpace space, float snap, float x, float y,
                         float width, float height, GizmoLayer layer);

// True while the cursor is over a handle, dragging or not. Only meaningful after a manipulate call
// this frame.
bool gizmoIsOver();

// Draws the gizmo for the selected entity over the viewport image and writes the result back.
//
// Returns true while a handle is held, so the caller suppresses click-picking for that frame —
// otherwise letting go of a drag would re-select whatever is behind the object.
bool drawViewportGizmo(EditorContext& context, ViewportGizmoState& state, float x, float y,
                       float width, float height, bool& outChanged);

// Turns a click at `localX`/`localY` (relative to the viewport image) into a selection. Selects the
// entity that owns what was hit, falls back to the raw object index for hosts with no world, and
// clears the selection on a click into empty space — which is how every editor lets you deselect.
//
// False when there was nothing to test against (no camera, no scene).
bool pickAtViewportPosition(EditorContext& context, float localX, float localY, float width,
                            float height);

} // namespace tucano::editor
