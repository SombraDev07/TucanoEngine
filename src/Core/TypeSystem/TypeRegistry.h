#pragma once

#include "Core/TypeSystem/TypeID.h"
#include "Core/TypeSystem/TypeInfo.h"

#include <string_view>
#include <unordered_map>
#include <vector>

// TypeRegistry — every registered type, looked up by id or name.
//
// Derived from Esoterica (MIT) — Code/Base/TypeSystem/TypeRegistry.h
//
// The registry is what makes reflection *generic*. A TypeInfo on its own only helps code that
// already knows the type; the registry lets the property grid walk into a nested struct it has
// never heard of, and lets a loader turn a type name in a file back into an object.
//
// Registration is by pointer to static data — the registry never owns or copies a TypeInfo.
//
// Until the Reflector (P3-03) generates registrations, types register themselves through
// TUCANO_REFLECT_TYPE below.

namespace tucano {

// Specialised by TUCANO_REFLECT_TYPE_BEGIN so `registry.find<WaterParams>()` works. Declared before
// the registry because find<T>() names it.
template <typename T>
struct TypeName;

class TypeRegistry {
public:
	// The process-wide registry. A singleton because type descriptions are process-wide facts and
	// threading a registry through every call site buys nothing.
	static TypeRegistry& instance();

	// Registering the same id twice keeps the first and returns false: a duplicate almost always
	// means two types share a name, and silently replacing one makes that bug invisible.
	bool registerType(const TypeInfo* info);

	const TypeInfo* find(TypeID id) const;
	const TypeInfo* find(std::string_view name) const;

	template <typename T>
	const TypeInfo* find() const {
		return find(TypeID(TypeName<T>::value));
	}

	// Null when the type is unknown or has no create function.
	void* createInstance(TypeID id) const;
	void destroyInstance(TypeID id, void* instance) const;

	// Walks the parent chain. A type is considered derived from itself, matching how the check reads
	// at call sites ("can I treat this as a Component?").
	bool isDerivedFrom(TypeID derived, TypeID base) const;

	std::vector<const TypeInfo*> allTypes() const;
	size_t size() const { return m_types.size(); }

	// For tests that need a clean slate. Does not free anything — the registry never owned it.
	void clear();

private:
	TypeRegistry() = default;

	std::unordered_map<TypeID, const TypeInfo*> m_types;
	std::unordered_map<std::string_view, const TypeInfo*> m_byName;
};

} // namespace tucano

// ── Declaration ─────────────────────────────────────────────────────────────
//
// Hand-written until the Reflector generates it. The pattern is deliberately the same shape the
// generator will emit, so switching over is a delete rather than a rewrite:
//
//   TUCANO_REFLECT_TYPE_BEGIN(WaterParams)
//     TUCANO_PROPERTY(enabled, Bool, .label = "Enabled")
//     TUCANO_PROPERTY(waterLevel, Float, .label = "Water level", .minValue = -200.0f, .maxValue = 200.0f)
//   TUCANO_REFLECT_TYPE_END(WaterParams)

#define TUCANO_REFLECT_TYPE_BEGIN(Type)                                                     \
	namespace tucano {                                                                       \
	template <>                                                                              \
	struct TypeName<::tucano::Type> {                                                        \
		static constexpr const char* value = #Type;                                          \
	};                                                                                       \
	namespace typeinfo_##Type {                                                              \
	using Owner = ::tucano::Type;                                                            \
	inline const PropertyInfo kProperties[] = {

// Same, for a type in a namespace nested under `tucano` — `tucano::ecs::NameComponent` and so on.
//
// A separate macro rather than passing "ecs::NameComponent" to the one above, because that name is
// also pasted into an identifier (`typeinfo_##Type`), and `typeinfo_ecs::NameComponent` does not
// compile. The registered name stays the short one: TypeIDs live in files, and "NameComponent" is
// what should still resolve after the type is moved between namespaces.
#define TUCANO_REFLECT_TYPE_BEGIN_NS(Ns, Type)                                               \
	namespace tucano {                                                                       \
	template <>                                                                              \
	struct TypeName<::tucano::Ns::Type> {                                                    \
		static constexpr const char* value = #Type;                                          \
	};                                                                                       \
	namespace typeinfo_##Type {                                                              \
	using Owner = ::tucano::Ns::Type;                                                        \
	inline const PropertyInfo kProperties[] = {

// `meta` uses designated initialisers, so a property declares only the presentation it cares about.
#define TUCANO_PROPERTY(member, core, ...)                                                   \
	PropertyInfo{#member,                                                                    \
	             CoreType::core,                                                             \
	             TypeID{},                                                                   \
	             static_cast<uint32_t>(offsetof(Owner, member)),                             \
	             static_cast<uint32_t>(sizeof(Owner::member)),                               \
	             0,                                                                          \
	             CoreType::Unknown,                                                           \
	             PropertyMetadata{__VA_ARGS__}},

// Enum-typed member; `enumType` is the enum's registered name. A separate macro from TUCANO_PROPERTY
// because the TypeID has to point at the enum — without it the grid finds no constants and falls
// back to a number spinner, and the serialiser writes the integer instead of the name.
#define TUCANO_PROPERTY_ENUM(member, enumType, ...)                                          \
	PropertyInfo{#member,                                                                    \
	             CoreType::Enum,                                                             \
	             TypeID{#enumType},                                                          \
	             static_cast<uint32_t>(offsetof(Owner, member)),                             \
	             static_cast<uint32_t>(sizeof(Owner::member)),                               \
	             0,                                                                          \
	             CoreType::Unknown,                                                          \
	             PropertyMetadata{__VA_ARGS__}},

// Nested registered struct; `sub` is its type name.
#define TUCANO_PROPERTY_STRUCT(member, sub, ...)                                             \
	PropertyInfo{#member,                                                                    \
	             CoreType::Struct,                                                           \
	             TypeID{#sub},                                                               \
	             static_cast<uint32_t>(offsetof(Owner, member)),                             \
	             static_cast<uint32_t>(sizeof(Owner::member)),                               \
	             0,                                                                          \
	             CoreType::Unknown,                                                           \
	             PropertyMetadata{__VA_ARGS__}},

#define TUCANO_REFLECT_TYPE_END(Type)                                                       \
	};                                                                                       \
	inline const TypeInfo kTypeInfo{                                                         \
	    #Type,                                                                               \
	    TypeID{#Type},                                                                       \
	    static_cast<uint32_t>(sizeof(Owner)),                                                \
	    static_cast<uint32_t>(alignof(Owner)),                                               \
	    kProperties,                                                                         \
	    static_cast<uint32_t>(sizeof(kProperties) / sizeof(kProperties[0])),                 \
	    TypeID{},                                                                            \
	    nullptr,                                                                             \
	    0,                                                                                   \
	    []() -> void* { return new Owner(); },                                               \
	    [](void* p) { delete static_cast<Owner*>(p); }};                                     \
	/* Registration runs at static-init time. The registry is a function-local static, so it  \
	   is constructed on first use and cannot lose a race with this. */                       \
	inline const bool kRegistered = TypeRegistry::instance().registerType(&kTypeInfo);       \
	}                                                                                        \
	}

// ── Enums ───────────────────────────────────────────────────────────────────
//
// An enum registers its constants instead of its properties, so a combo shows names and a file
// stores names:
//
//   TUCANO_REFLECT_ENUM_BEGIN(GITier)
//     TUCANO_ENUM_CONSTANT(Off, "Off")
//     TUCANO_ENUM_CONSTANT(Low, "Low")
//   TUCANO_REFLECT_ENUM_END(GITier)
//
// Storing the *name* is the whole point: inserting a constant in the middle renumbers everything
// after it, and a file full of integers would silently start meaning something else.

#define TUCANO_REFLECT_ENUM_BEGIN(Enum)                                                      \
	namespace tucano {                                                                       \
	template <>                                                                              \
	struct TypeName<::tucano::Enum> {                                                        \
		static constexpr const char* value = #Enum;                                          \
	};                                                                                       \
	namespace enuminfo_##Enum {                                                              \
	using Owner = ::tucano::Enum;                                                            \
	inline const EnumConstant kConstants[] = {

// Same, for an enum in a namespace nested under `tucano` — see TUCANO_REFLECT_TYPE_BEGIN_NS for why
// this cannot just take a qualified name.
#define TUCANO_REFLECT_ENUM_BEGIN_NS(Ns, Enum)                                               \
	namespace tucano {                                                                       \
	template <>                                                                              \
	struct TypeName<::tucano::Ns::Enum> {                                                    \
		static constexpr const char* value = #Enum;                                          \
	};                                                                                       \
	namespace enuminfo_##Enum {                                                              \
	using Owner = ::tucano::Ns::Enum;                                                        \
	inline const EnumConstant kConstants[] = {

// `label` is what the combo shows; the name is what the file stores. Two fields because renaming a
// constant for readability must not rewrite every scene that mentions it.
#define TUCANO_ENUM_CONSTANT(constant, ...)                                                  \
	EnumConstant{#constant, static_cast<int64_t>(Owner::constant), __VA_ARGS__},

#define TUCANO_REFLECT_ENUM_END(Enum)                                                        \
	};                                                                                       \
	inline const TypeInfo kTypeInfo{                                                         \
	    #Enum,                                                                               \
	    TypeID{#Enum},                                                                       \
	    static_cast<uint32_t>(sizeof(Owner)),                                                \
	    static_cast<uint32_t>(alignof(Owner)),                                               \
	    nullptr,                                                                             \
	    0,                                                                                   \
	    TypeID{},                                                                            \
	    kConstants,                                                                          \
	    static_cast<uint32_t>(sizeof(kConstants) / sizeof(kConstants[0])),                   \
	    /* No create/destroy: an enum is a value, and handing out `new GITier` would invite   \
	       treating it as an object the editor can instantiate. */                            \
	    nullptr,                                                                              \
	    nullptr};                                                                            \
	inline const bool kRegistered = TypeRegistry::instance().registerType(&kTypeInfo);       \
	}                                                                                        \
	}
