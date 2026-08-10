#include "ECS/SceneFile.h"

#include "Core/Json.h"
#include "Core/TypeSystem/Serialization.h"
#include "Core/TypeSystem/TypeRegistry.h"
#include "ECS/AuthoringComponents.h"
#include "ECS/Components.h"
#include "ECS/EntityManager.h"
#include "ECS/World.h"
#include "Renderer/Weather/CloudSystem.h"
#include "Renderer/Weather/FogParams.h"
#include "Renderer/Weather/RainParams.h"
#include "Renderer/Weather/WaterParams.h"

#include <cstring>
#include <vector>

namespace tucano::ecs {
namespace {

constexpr int kFormatVersion = 1;

// The components a scene file carries, paired with the reflected type that describes their fields.
//
// An explicit table rather than "everything registered": `PhysicsBodyComponent` holds a Jolt body
// id and `RenderObjectComponent` an index into the render scene. Both are rebuilt on load, and
// writing them would bake this run's addresses into a file that outlives the run.
// The list itself lives in AuthoringComponents (C-07): what a scene writes, what the Inspector
// shows and what Add Component offers are the same four things, and three copies of that list were
// three chances to disagree.

// Every live entity, in a stable order. Archetype/chunk order is an implementation detail that
// shifts as components are added, so the list is what a save iterates, not the storage.
std::vector<Entity> liveEntities(World& world) {
	std::vector<Entity> out;
	for (EntityManager::Archetype& archetype : world.entities().archetypes()) {
		for (EntityManager::Chunk& chunk : archetype.chunks) {
			for (uint32_t i = 0; i < chunk.count; ++i) {
				out.push_back(chunk.entities[i]);
			}
		}
	}
	return out;
}

void appendEnvironment(std::string& out, const SceneEnvironment& environment) {
	const auto block = [&out](const char* key, const TypeInfo* type, const void* instance,
	                          bool& first) {
		if (type == nullptr || instance == nullptr) return;
		if (!first) out += ",\n";
		first = false;
		out += "    \"";
		out += key;
		out += "\": ";
		out += serializeToJson(*type, instance, 2);
	};

	const auto& registry = TypeRegistry::instance();
	out += "  \"environment\": {\n";
	bool first = true;
	block("WaterParams", registry.find(TypeID{"WaterParams"}), environment.water, first);
	block("FogParams", registry.find(TypeID{"FogParams"}), environment.fog, first);
	block("CloudParams", registry.find(TypeID{"CloudParams"}), environment.clouds, first);
	block("RainParams", registry.find(TypeID{"RainParams"}), environment.rain, first);
	out += "\n  }";
}

void readEnvironment(const core::JsonValue& node, const SceneEnvironment& environment,
                     std::string* err) {
	const auto block = [&](const char* key, void* instance) {
		if (instance == nullptr) return;
		const core::JsonValue* value = node.find(key);
		if (value == nullptr) return; // absent means "leave what is already loaded"
		const TypeInfo* type = TypeRegistry::instance().find(TypeID{key});
		if (type == nullptr) return;
		// Overlaid onto whatever the block already holds, so a key the file omits keeps the running
		// value instead of being reset.
		std::string blockError;
		readStructInto(*value, *type, instance, &blockError);
		if (!blockError.empty() && err != nullptr) {
			if (!err->empty()) *err += "; ";
			*err += blockError;
		}
	};

	block("WaterParams", environment.water);
	block("FogParams", environment.fog);
	block("CloudParams", environment.clouds);
	block("RainParams", environment.rain);
}

// Writes one entity's components as a JSON object. Shared by the scene writer and by
// entityToJson so the two can never disagree about what an entity is.
std::string entityBody(World& world, Entity entity, int depth) {
	const auto& registry = TypeRegistry::instance();
	std::string body;
	bool first = true;
	for (size_t i = 0; i < authoringComponentCount(); ++i) {
		const AuthoringComponentInfo& entry = authoringComponents()[i];
		if (*entry.id == kInvalidEntity) continue;
		if (!world.entities().has(entity, *entry.id)) continue;

		const TypeInfo* type = registry.find(TypeID{entry.typeName});
		const void* data = world.entities().get(entity, *entry.id);
		if (type == nullptr || data == nullptr) continue;

		if (!first) body += ",\n";
		first = false;
		body.append(static_cast<size_t>(depth) * 2, ' ');
		body += '"';
		body += entry.key;
		body += "\": ";
		body += serializeToJson(*type, data, depth);
	}
	return body;
}

// Adds the components described by `node` to an entity that already exists.
void applyComponents(const core::JsonValue& node, World& world, Entity entity, std::string* err) {
	const auto& registry = TypeRegistry::instance();
	for (size_t i = 0; i < authoringComponentCount(); ++i) {
		const AuthoringComponentInfo& entry = authoringComponents()[i];
		const core::JsonValue* componentNode = node.find(entry.key);
		if (componentNode == nullptr || !componentNode->isObject()) continue;
		if (*entry.id == kInvalidEntity) continue;

		const TypeInfo* type = registry.find(TypeID{entry.typeName});
		if (type == nullptr) continue;

		// Default-constructed, not zeroed. Deserialisation leaves a key it does not find alone, and
		// that rule only means the right thing if "alone" is the type's default — a scene written
		// before a field existed would otherwise load it as zero, which for a range or an intensity
		// is not a default, it is broken.
		void* data = entry.add(world, entity);
		if (data == nullptr) continue;
		// add() zeroes the storage; seed the type's real defaults so a property the file omits lands
		// on the default rather than on zero.
		if (type->create != nullptr) {
			void* defaults = type->create();
			if (defaults != nullptr) {
				std::memcpy(data, defaults, type->size);
				type->destroy(defaults);
			}
		}
		std::string componentError;
		readStructInto(*componentNode, *type, data, &componentError);
		if (!componentError.empty() && err != nullptr) {
			if (!err->empty()) *err += "; ";
			*err += componentError;
		}
	}
}

} // namespace

std::string entityToJson(World& world, Entity entity) {
	if (!world.alive(entity)) return "{}";
	return "{\n" + entityBody(world, entity, 1) + "\n}";
}

Entity entityFromJson(World& world, std::string_view json, std::string* err) {
	core::JsonValue root;
	std::string parseError;
	if (!core::JsonValue::parse(json, root, &parseError)) {
		if (err) *err = "invalid JSON: " + parseError;
		return kInvalidEntity;
	}
	if (!root.isObject()) {
		if (err) *err = "entity is not an object";
		return kInvalidEntity;
	}
	const Entity entity = world.create();
	applyComponents(root, world, entity, err);
	return entity;
}

std::string sceneToJson(World& world, const SceneEnvironment& environment) {
	std::string out;
	out += "{\n";
	out += "  \"format\": \"tuscene\",\n";
	out += "  \"version\": ";
	out += std::to_string(kFormatVersion);
	out += ",\n";
	out += "  \"entities\": [\n";

	const auto& registry = TypeRegistry::instance();
	const std::vector<Entity> entities = liveEntities(world);

	bool firstEntity = true;
	for (const Entity entity : entities) {
		// Collect first: an entity with only runtime components produces no object at all rather
		// than an empty one, which keeps a file readable.
		const std::string body = entityBody(world, entity, 3);
		if (body.empty()) continue;

		if (!firstEntity) out += ",\n";
		firstEntity = false;
		out += "    {\n";
		out += body;
		out += "\n    }";
	}

	out += "\n  ],\n";
	appendEnvironment(out, environment);
	out += "\n}\n";
	return out;
}

bool sceneFromJson(std::string_view json, World& world, const SceneEnvironment& environment,
                   std::string* err) {
	core::JsonValue root;
	std::string parseError;
	if (!core::JsonValue::parse(json, root, &parseError)) {
		if (err) *err = "invalid JSON: " + parseError;
		return false;
	}
	if (!root.isObject()) {
		if (err) *err = "root is not an object";
		return false;
	}
	const core::JsonValue* format = root.find("format");
	if (format == nullptr || format->asString() != "tuscene") {
		if (err) *err = "not a tuscene file";
		return false;
	}
	// A newer version is refused rather than half-read: silently ignoring a field the format added
	// is how a save gets quietly downgraded when it is written back.
	const core::JsonValue* version = root.find("version");
	if (version != nullptr && version->asInt(kFormatVersion) > kFormatVersion) {
		if (err) {
			*err = "scene was written by a newer version (" + std::to_string(version->asInt(0)) + ")";
		}
		return false;
	}

	if (err) err->clear();

	// Clear before loading. Entities are destroyed through the manager so archetypes stay coherent.
	for (const Entity entity : liveEntities(world)) {
		world.destroy(entity);
	}

	const auto& registry = TypeRegistry::instance();
	const core::JsonValue* entities = root.find("entities");
	if (entities != nullptr && entities->isArray()) {
		for (const core::JsonValue& entityNode : entities->arr) {
			if (!entityNode.isObject()) continue;

			const Entity entity = world.create();
			applyComponents(entityNode, world, entity, err);
		}
	}

	if (const core::JsonValue* env = root.find("environment"); env != nullptr && env->isObject()) {
		readEnvironment(*env, environment, err);
	}
	return true;
}

bool saveScene(const std::string& path, World& world, const SceneEnvironment& environment,
               std::string* err) {
	return writeTextAtomically(path, sceneToJson(world, environment), err);
}

bool loadScene(const std::string& path, World& world, const SceneEnvironment& environment,
               std::string* err) {
	std::string text;
	if (!readTextFile(path, text, err)) return false;
	return sceneFromJson(text, world, environment, err);
}

} // namespace tucano::ecs
