#pragma once

#include "Core/TypeSystem/TypeID.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

// Runtime description of a type and its properties.
//
// Derived from Esoterica (MIT) — Code/Base/TypeSystem/{TypeInfo,PropertyInfo}.h
//
// This is what lets one property grid edit every settings block in the engine instead of one hand
// written panel per struct. A property is described by where it lives (offset), what it is
// (CoreType plus a TypeID for structs and enums), and how it should be presented (metadata) — the
// third part matters because a float that is a probability and a float that is a distance in metres
// need different widgets, and only the author knows which is which.
//
// Descriptions are static data. A TypeInfo is written once, by hand today and by the Reflector in
// P3-03, and lives for the process; nothing here allocates.

namespace tucano {

// What a property is made of. Deliberately closed: an editor has to have a widget for each of
// these, so adding one is a decision, not an accident.
enum class CoreType : uint8_t {
	Unknown = 0,
	Bool,
	Int32,
	UInt32,
	Int64,
	UInt64,
	Float,
	Double,
	String,
	Vec2,
	Vec3,
	Vec4,
	Quat,
	// Vec3/Vec4 shown as a colour swatch rather than numbers. Same storage as the vector.
	Color,
	// Integer whose valid values come from an enum registered in the TypeRegistry.
	Enum,
	// Nested registered struct; `typeId` names it.
	Struct,
	// Contiguous run of `arrayCount` elements of `coreType`.
	Array,
};

// How a property should be presented. Everything here is authoring intent, not engine contract —
// clamping a slider does not clamp the value the engine reads.
struct PropertyMetadata {
	const char* label = "";    // human-facing; falls back to the property name when empty
	const char* tooltip = "";
	const char* category = ""; // groups rows under a header in the grid
	float minValue = 0.0f;
	float maxValue = 0.0f; // max <= min means "unbounded", which is the honest default
	float step = 0.0f;     // 0 means the editor picks
	bool readOnly = false;
	// Kept out of a saved file. For derived or transient state that would be misleading on disk.
	bool transient = false;
	// A String property that names a file rather than holding free text: "mesh", "texture", "hdri",
	// "scene", "text", "any". The editor draws a picker instead of a text box, which is the
	// difference between choosing an asset and typing a path that may not exist. Ignored on every
	// other CoreType. Empty means the string really is free text.
	const char* assetKind = "";
};

struct PropertyInfo {
	const char* name = "";
	CoreType coreType = CoreType::Unknown;
	// Set for Struct and Enum properties, and for Array elements that are structs.
	TypeID typeId;
	uint32_t offset = 0;
	uint32_t size = 0;
	// Elements for Array, 0 otherwise.
	uint32_t arrayCount = 0;
	// Element type when coreType is Array.
	CoreType elementType = CoreType::Unknown;
	PropertyMetadata meta;

	const char* displayLabel() const {
		return (meta.label != nullptr && meta.label[0] != '\0') ? meta.label : name;
	}

	void* addressIn(void* instance) const {
		return instance != nullptr ? static_cast<uint8_t*>(instance) + offset : nullptr;
	}
	const void* addressIn(const void* instance) const {
		return instance != nullptr ? static_cast<const uint8_t*>(instance) + offset : nullptr;
	}

	// Typed access. The caller is responsible for asking for the type the property actually is —
	// the grid does that by switching on coreType, and nothing else should be reaching in here.
	template <typename T>
	T& valueIn(void* instance) const {
		return *static_cast<T*>(addressIn(instance));
	}
	template <typename T>
	const T& valueIn(const void* instance) const {
		return *static_cast<const T*>(addressIn(instance));
	}
};

// One entry of an enum, so a combo box can show names instead of numbers.
struct EnumConstant {
	const char* name = "";
	int64_t value = 0;
	const char* label = "";
};

struct TypeInfo {
	const char* name = "";
	TypeID id;
	uint32_t size = 0;
	uint32_t alignment = 0;

	const PropertyInfo* properties = nullptr;
	uint32_t propertyCount = 0;

	// Single inheritance is all the editor needs; invalid means "no base".
	TypeID parentId;

	// Enum types carry constants instead of properties.
	const EnumConstant* enumConstants = nullptr;
	uint32_t enumConstantCount = 0;

	// Null for types the editor may only inspect, not instantiate.
	void* (*create)() = nullptr;
	void (*destroy)(void*) = nullptr;

	bool isEnum() const { return enumConstantCount > 0; }

	const PropertyInfo* findProperty(std::string_view propertyName) const {
		for (uint32_t i = 0; i < propertyCount; ++i) {
			if (propertyName == properties[i].name) return &properties[i];
		}
		return nullptr;
	}

	const EnumConstant* findEnumConstant(int64_t value) const {
		for (uint32_t i = 0; i < enumConstantCount; ++i) {
			if (enumConstants[i].value == value) return &enumConstants[i];
		}
		return nullptr;
	}
};

} // namespace tucano
