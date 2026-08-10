#pragma once

// Serialization driven by reflection — any registered type, to and from JSON.
//
// C-01 of the roadmap, and the thing the editor is missing to be an editor: today every setting
// tuned in a panel is lost when the process exits. This is also what makes Play mode cheap later
// (snapshot, run, restore) and what a `.tuscene` is built out of.
//
//   const std::string text = serializeToJson(material);
//   Material restored;
//   deserializeFromJson(text, restored);
//
// Four rules decide how this behaves across versions of a file. They are the difference between a
// format that survives the project growing and one that corrupts saves quietly:
//
//   1. The key is the property `name`, never its `label`. Labels are presentation and are expected
//      to change; renaming "Metallic" to "Metalness" must not orphan the data.
//   2. A property missing from the file keeps the value the instance already has. An older save
//      then loads with new fields at their defaults instead of at zero.
//   3. A key in the file that no property matches is ignored. A save from a newer build still
//      loads in an older one, minus what it cannot represent.
//   4. `transient` properties are not written. That flag exists precisely to say "derived state,
//      misleading on disk".
//
// Enums are written by constant *name*, not by number, so inserting a value into the middle of an
// enum does not silently reinterpret every file ever saved.

#include "Core/Json.h"
#include "Core/TypeSystem/TypeRegistry.h"

#include <string>
#include <string_view>

namespace tucano {

struct TypeInfo;

// ── Writing ─────────────────────────────────────────────────────────────────

// `indent` is the starting depth; the output is pretty-printed because saved files get read and
// diffed by people, and a one-line blob makes a review useless.
std::string serializeToJson(const TypeInfo& type, const void* instance, int indent = 0);

template <typename T>
std::string serializeToJson(const T& instance) {
	const TypeInfo* type = TypeRegistry::instance().find(TypeID(TypeName<T>::value));
	return type != nullptr ? serializeToJson(*type, &instance) : std::string("{}");
}

// ── Reading ─────────────────────────────────────────────────────────────────

// Fills `instance` from `json`. The instance is *modified in place*, so it starts from whatever
// defaults the caller constructed it with — that is what makes rule 2 above work.
//
// Returns false only when the text is not valid JSON or its root is not an object. A property with
// the wrong JSON type is skipped and reported through `err` without failing the whole load: one bad
// field should not cost the rest of the scene.
bool deserializeFromJson(std::string_view json, const TypeInfo& type, void* instance,
                         std::string* err = nullptr);

template <typename T>
bool deserializeFromJson(std::string_view json, T& instance, std::string* err = nullptr) {
	const TypeInfo* type = TypeRegistry::instance().find(TypeID(TypeName<T>::value));
	if (type == nullptr) {
		if (err) *err = "type is not registered";
		return false;
	}
	return deserializeFromJson(json, *type, &instance, err);
}

// ── Building blocks ─────────────────────────────────────────────────────────
//
// Exposed because a container format (a `.tuscene` is a list of entities, each a bag of components)
// writes its own structure but wants reflection for the leaves. Without these it would have to
// re-serialise every sub-object to text and parse it back.

// Overlays an already-parsed JSON object onto an instance. Same four version rules as
// deserializeFromJson; field problems are appended to `err` rather than failing.
void readStructInto(const core::JsonValue& node, const TypeInfo& type, void* instance,
                    std::string* err = nullptr);

// ── Files ───────────────────────────────────────────────────────────────────

// Writes through a temporary and renames over the target, so a crash mid-write leaves the previous
// file intact. Shared by every format the editor saves.
bool writeTextAtomically(const std::string& path, const std::string& text,
                         std::string* err = nullptr);
bool readTextFile(const std::string& path, std::string& out, std::string* err = nullptr);


// Writes through a temporary and renames over the target: a crash mid-write leaves the previous
// file intact rather than a truncated one. Losing a scene to a power cut is not acceptable.
bool saveToFile(const std::string& path, const TypeInfo& type, const void* instance,
                std::string* err = nullptr);
bool loadFromFile(const std::string& path, const TypeInfo& type, void* instance,
                  std::string* err = nullptr);

template <typename T>
bool saveToFile(const std::string& path, const T& instance, std::string* err = nullptr) {
	const TypeInfo* type = TypeRegistry::instance().find(TypeID(TypeName<T>::value));
	if (type == nullptr) {
		if (err) *err = "type is not registered";
		return false;
	}
	return saveToFile(path, *type, &instance, err);
}

template <typename T>
bool loadFromFile(const std::string& path, T& instance, std::string* err = nullptr) {
	const TypeInfo* type = TypeRegistry::instance().find(TypeID(TypeName<T>::value));
	if (type == nullptr) {
		if (err) *err = "type is not registered";
		return false;
	}
	return loadFromFile(path, *type, &instance, err);
}

} // namespace tucano
