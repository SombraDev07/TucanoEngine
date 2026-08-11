#include "Editor/SceneCommands.h"

#include "Editor/EditorContext.h"
#include "Editor/UndoStack.h"
#include "ECS/AuthoringComponents.h"
#include "ECS/Components.h"
#include "ECS/SceneFile.h"
#include "ECS/World.h"

#include "Core/TypeSystem/TypeRegistry.h"

#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace tucano::editor {
namespace {

// Create and delete are the same action seen from two directions, so one class covers both.
//
// The entity is carried as serialised components rather than as an id: an id whose entity has been
// destroyed means nothing, while the components are exactly what has to come back. `m_entity` is
// updated every time it is recreated, so a second undo/redo pair operates on the live one.
class EntityLifetimeAction final : public UndoAction {
public:
	EntityLifetimeAction(std::string name, ecs::World& world, ecs::Entity entity, bool createdIt)
	    : m_name(std::move(name)), m_world(world), m_entity(entity), m_created(createdIt),
	      m_snapshot(ecs::entityToJson(world, entity)) {}

	void undo() override { m_created ? destroy() : restore(); }
	void redo() override { m_created ? restore() : destroy(); }
	const std::string& name() const override { return m_name; }

	ecs::Entity entity() const { return m_entity; }

private:
	void destroy() {
		if (!m_world.alive(m_entity)) return;
		// Re-snapshot before destroying: the entity may have been edited since the action was
		// created, and undo should bring back what was there, not what was there originally.
		m_snapshot = ecs::entityToJson(m_world, m_entity);
		m_world.destroy(m_entity);
	}

	void restore() {
		if (m_world.alive(m_entity)) return;
		m_entity = ecs::entityFromJson(m_world, m_snapshot);
	}

	std::string m_name;
	ecs::World& m_world;
	ecs::Entity m_entity = ecs::kInvalidEntity;
	bool m_created = false;
	std::string m_snapshot;
};

// Add and remove a component, in one class for the same reason as create/delete: they are the same
// action seen from two directions.
//
// The component's value is carried as raw bytes rather than as JSON. Authoring components are
// `static_assert`-ed trivially copyable (the ECS requires it), so bytes are exact and cheap — and
// unlike JSON they cannot lose a field the serialiser does not know about.
class ComponentLifetimeAction final : public UndoAction {
public:
	ComponentLifetimeAction(std::string name, ecs::World& world, ecs::Entity entity,
	                        const ecs::AuthoringComponentInfo& info, uint32_t size, bool addedIt)
	    : m_name(std::move(name)), m_world(world), m_entity(entity), m_info(&info), m_size(size),
	      m_added(addedIt) {
		m_bytes.resize(size);
		capture();
	}

	void undo() override { m_added ? strip() : restore(); }
	void redo() override { m_added ? restore() : strip(); }
	const std::string& name() const override { return m_name; }

private:
	void capture() {
		if (!m_world.alive(m_entity)) return;
		const void* data = m_world.entities().get(m_entity, *m_info->id);
		if (data != nullptr && m_size > 0) std::memcpy(m_bytes.data(), data, m_size);
	}

	void strip() {
		if (!m_world.alive(m_entity)) return;
		// Re-capture first: the component may have been edited since, and undo should bring back
		// what was on screen rather than what was there when the action was recorded.
		capture();
		m_info->remove(m_world, m_entity);
	}

	void restore() {
		if (!m_world.alive(m_entity)) return;
		void* data = m_info->add(m_world, m_entity);
		if (data != nullptr && m_size > 0) std::memcpy(data, m_bytes.data(), m_size);
	}

	std::string m_name;
	ecs::World& m_world;
	ecs::Entity m_entity = ecs::kInvalidEntity;
	const ecs::AuthoringComponentInfo* m_info = nullptr;
	uint32_t m_size = 0;
	bool m_added = false;
	std::vector<uint8_t> m_bytes;
};

class RenameAction final : public UndoAction {
public:
	RenameAction(std::string name, ecs::World& world, ecs::Entity entity, std::string before,
	             std::string after)
	    : m_name(std::move(name)), m_world(world), m_entity(entity), m_before(std::move(before)),
	      m_after(std::move(after)) {}

	void undo() override { apply(m_before); }
	void redo() override { apply(m_after); }
	const std::string& name() const override { return m_name; }

	bool mergeWith(const UndoAction& next) override {
		// Typing a name is one gesture, not one step per keystroke.
		const auto* other = dynamic_cast<const RenameAction*>(&next);
		if (other == nullptr || other->m_entity != m_entity) return false;
		m_after = other->m_after;
		return true;
	}

private:
	void apply(const std::string& text) {
		if (auto* component = m_world.get<ecs::NameComponent>(m_entity)) component->name = text;
	}

	std::string m_name;
	ecs::World& m_world;
	ecs::Entity m_entity = ecs::kInvalidEntity;
	std::string m_before;
	std::string m_after;
};

// One gizmo drag, as one step.
//
// The component is looked up by entity id on every undo and redo rather than held by pointer.
// `EntityManager` moves an entity's components to another archetype with memcpy when a component is
// added or removed, so a `TransformComponent*` captured here stops pointing at the entity the first
// time someone uses Add Component — and writes into whatever moved into that slot instead.
class TransformAction final : public UndoAction {
public:
	TransformAction(std::string name, ecs::World& world, ecs::Entity entity,
	                const ecs::TransformComponent& before, const ecs::TransformComponent& after)
	    : m_name(std::move(name)), m_world(world), m_entity(entity), m_before(before),
	      m_after(after) {}

	void undo() override { apply(m_before); }
	void redo() override { apply(m_after); }
	const std::string& name() const override { return m_name; }

	// No mergeWith, unlike RenameAction: a drag already arrives here as one finished gesture, so
	// merging consecutive ones would fold two separate moves into a single undo step.

private:
	void apply(const ecs::TransformComponent& value) {
		if (!m_world.alive(m_entity)) return;
		if (auto* transform = m_world.get<ecs::TransformComponent>(m_entity)) *transform = value;
	}

	std::string m_name;
	ecs::World& m_world;
	ecs::Entity m_entity = ecs::kInvalidEntity;
	ecs::TransformComponent m_before;
	ecs::TransformComponent m_after;
};

} // namespace

ecs::Entity createEntity(ecs::World& world, UndoStack* undo, std::string_view name) {
	const ecs::Entity entity = world.create();
	world.add<ecs::NameComponent>(entity)->name = name;
	// Every authored entity has a place in the world, even one that draws nothing — otherwise it
	// cannot be moved, and an entity that cannot be moved is not much of an entity.
	world.add<ecs::TransformComponent>(entity);

	if (undo != nullptr) {
		undo->push(std::make_unique<EntityLifetimeAction>("Create entity", world, entity, true));
		undo->breakMerge();
	}
	return entity;
}

ecs::Entity duplicateEntity(ecs::World& world, UndoStack* undo, ecs::Entity source) {
	if (!world.alive(source)) return ecs::kInvalidEntity;

	const ecs::Entity copy = ecs::entityFromJson(world, ecs::entityToJson(world, source));
	if (copy == ecs::kInvalidEntity) return copy;

	if (undo != nullptr) {
		undo->push(std::make_unique<EntityLifetimeAction>("Duplicate entity", world, copy, true));
		undo->breakMerge();
	}
	return copy;
}

bool deleteEntity(ecs::World& world, UndoStack* undo, ecs::Entity entity) {
	if (!world.alive(entity)) return false;

	if (undo != nullptr) {
		// The action snapshots in its constructor, so it has to be built before the entity dies.
		auto action = std::make_unique<EntityLifetimeAction>("Delete entity", world, entity, false);
		action->redo(); // performs the destroy
		undo->push(std::move(action));
		undo->breakMerge();
		return true;
	}
	world.destroy(entity);
	return true;
}

bool renameEntity(ecs::World& world, UndoStack* undo, ecs::Entity entity, std::string_view name) {
	auto* component = world.get<ecs::NameComponent>(entity);
	if (component == nullptr) return false;

	const std::string before = component->name.str();
	if (before == name) return false;

	component->name = name;
	if (undo != nullptr) {
		undo->push(std::make_unique<RenameAction>("Rename entity", world, entity, before,
		                                          std::string(name)));
	}
	return true;
}

bool addComponent(ecs::World& world, UndoStack* undo, ecs::Entity entity,
                  const ecs::AuthoringComponentInfo& info) {
	if (!world.alive(entity) || *info.id == ecs::kInvalidEntity) return false;
	// Already there: adding twice would silently reset the values someone just tuned.
	if (world.entities().has(entity, *info.id)) return false;

	if (info.add(world, entity) == nullptr) return false;
	if (undo != nullptr) {
		const TypeInfo* type = TypeRegistry::instance().find(TypeID{info.typeName});
		undo->push(std::make_unique<ComponentLifetimeAction>(
		    std::string("Add ") + info.label, world, entity, info,
		    type != nullptr ? type->size : 0u, /*addedIt=*/true));
	}
	return true;
}

bool removeComponent(ecs::World& world, UndoStack* undo, ecs::Entity entity,
                     const ecs::AuthoringComponentInfo& info) {
	if (!world.alive(entity) || *info.id == ecs::kInvalidEntity) return false;
	if (!world.entities().has(entity, *info.id)) return false;
	// A transform is what "where is it" means and a name is how the Outliner refers to the thing at
	// all. Removing either leaves an entity nobody can find or place, so the menu does not offer it
	// and this refuses even when called directly.
	if (info.essential) return false;

	if (undo != nullptr) {
		const TypeInfo* type = TypeRegistry::instance().find(TypeID{info.typeName});
		// Constructed before the removal: it captures the current bytes in its constructor, which is
		// the only moment they still exist.
		auto action = std::make_unique<ComponentLifetimeAction>(
		    std::string("Remove ") + info.label, world, entity, info,
		    type != nullptr ? type->size : 0u, /*addedIt=*/false);
		info.remove(world, entity);
		undo->push(std::move(action));
		return true;
	}
	info.remove(world, entity);
	return true;
}

bool pushTransformEdit(ecs::World& world, UndoStack* undo, ecs::Entity entity,
                       const ecs::TransformComponent& before, const char* what) {
	if (!world.alive(entity)) return false;
	const auto* current = world.get<ecs::TransformComponent>(entity);
	if (current == nullptr) return false;

	// Only the authored fields. `prevPosition`/`prevRotation` are interpolation state that follows
	// the value anyway, and comparing them would call a click that moved nothing a change.
	if (current->position == before.position && current->rotation == before.rotation &&
	    current->scale == before.scale) {
		return false;
	}

	if (undo != nullptr) {
		undo->push(std::make_unique<TransformAction>(what != nullptr ? what : "Transform", world,
		                                             entity, before, *current));
		undo->breakMerge();
	}
	return true;
}

// ── Context-aware wrappers ──────────────────────────────────────────────────

ecs::Entity createEntity(EditorContext& context, std::string_view name) {
	if (context.world == nullptr) return ecs::kInvalidEntity;
	const ecs::Entity entity = createEntity(*context.world, context.undo, name);
	// Selecting what was just created is what every editor does, and it is what makes the next
	// action (move it, rename it) land on the right thing.
	context.selectedEntity = entity;
	return entity;
}

ecs::Entity duplicateSelected(EditorContext& context) {
	if (!context.hasSelectedEntity()) return ecs::kInvalidEntity;
	const ecs::Entity copy = duplicateEntity(*context.world, context.undo, context.selectedEntity);
	if (copy != ecs::kInvalidEntity) context.selectedEntity = copy;
	return copy;
}

bool deleteSelected(EditorContext& context) {
	if (!context.hasSelectedEntity()) return false;
	const bool deleted = deleteEntity(*context.world, context.undo, context.selectedEntity);
	// The selection pointed at what was just destroyed. Undo will bring the entity back under a
	// different id, so there is nothing honest to re-select.
	if (deleted) context.clearSelection();
	return deleted;
}

} // namespace tucano::editor
