#pragma once

// Entity operations, as undoable commands.
//
// C-05 of the roadmap. Before this the Outliner could only delete, destructively and with no way
// back — "Duplicate", "Rename" and "Focus" were empty `{}` bodies in the context menu.
//
// Free functions over (world, undo) rather than methods on a tool: the Outliner has an
// `EditorContext`, not a `SceneTool`, and threading the tool through every panel to reach one undo
// stack would couple the panels to the workspace that happens to host them.
//
// **The id is not preserved across undo.** `EntityManager` assigns ids and offers no "create with
// this id", so an entity restored by undo comes back as a different entity. Anything holding the
// old id has to be told: these commands clear the selection when the entity it pointed at is
// destroyed or restored. That is a real limitation of the current ECS, written down rather than
// hidden — it will matter the moment entities reference each other (hierarchy, prefabs).

#include "ECS/AuthoringComponents.h"
#include "ECS/ComponentTypes.h"

#include <string>
#include <string_view>

namespace tucano::ecs {
class World;
struct TransformComponent;
}

namespace tucano::editor {

class UndoStack;
struct EditorContext;

// Creates an empty entity carrying a name and a transform — the two things every authored entity
// has. Returns kInvalidEntity only when there is no world.
ecs::Entity createEntity(ecs::World& world, UndoStack* undo, std::string_view name = "Entity");

// A copy of every serialisable component of `source`, as a new entity. Runtime-only components
// (physics body, render index) are deliberately not copied: they are rebuilt, and copying a Jolt
// body id would give two entities the same body.
ecs::Entity duplicateEntity(ecs::World& world, UndoStack* undo, ecs::Entity source);

// Destroys the entity. Undo brings it back with the same components and a **new id**.
bool deleteEntity(ecs::World& world, UndoStack* undo, ecs::Entity entity);

bool renameEntity(ecs::World& world, UndoStack* undo, ecs::Entity entity, std::string_view name);

// Adds a component **default-constructed** — a new light is a usable light, not a zeroed one.
// Returns false when the entity already has it, rather than silently resetting values someone just
// tuned. Undo removes it again.
bool addComponent(ecs::World& world, UndoStack* undo, ecs::Entity entity,
                  const ecs::AuthoringComponentInfo& info);

// Refuses on components marked essential (name, transform). Undo puts the component back with the
// exact bytes it had.
bool removeComponent(ecs::World& world, UndoStack* undo, ecs::Entity entity,
                     const ecs::AuthoringComponentInfo& info);

// Records a transform change that has **already been applied**, which is what a gizmo drag is: the
// value is written every frame so the object follows the mouse, and the step is one per gesture.
// `before` is the transform as it was when the drag started.
//
// False when nothing actually moved — clicking a handle without dragging must not push a step that
// does nothing, because the user then presses Ctrl+Z and watches nothing happen.
//
// The action re-looks-up the component from the entity id instead of holding a pointer to it:
// `EntityManager` moves components between archetypes with memcpy when one is added or removed, so
// a pointer into a chunk is only valid until the next Add Component.
bool pushTransformEdit(ecs::World& world, UndoStack* undo, ecs::Entity entity,
                       const ecs::TransformComponent& before, const char* what);

// Same operations, but taking the context so the selection follows what happened — a new entity
// becomes selected, and a deleted one stops being.
ecs::Entity createEntity(EditorContext& context, std::string_view name = "Entity");
ecs::Entity duplicateSelected(EditorContext& context);
bool deleteSelected(EditorContext& context);

} // namespace tucano::editor
