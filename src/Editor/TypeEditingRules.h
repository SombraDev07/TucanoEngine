#pragma once

// TypeEditingRules — rules that depend on the *value* being edited, not just on its declaration.
//
// Derived from Esoterica (MIT) — Code/EngineTools/PropertyGrid/PropertyGridTypeEditingRules.h
//
// P4-03 of the roadmap. The static half of that task — ranges and categories — already shipped as
// PropertyMetadata in P4-01/CP-17, because a range that never changes belongs on the declaration.
// What could not be expressed there is the conditional half: `Material::alphaCutoff` is only read
// when `alphaMask` is on, and `WaterParams` ignores every one of its 29 settings when `enabled` is
// off. Today the grid shows those as live, editable controls that do nothing — which is a lie the
// tooltip has to apologise for.
//
// Two states, because they say different things:
//   - hidden: this field does not apply at all. The row disappears.
//   - locked: this field applies but cannot be changed right now. The row greys out.
// Prefer locked when a condition gates many rows: hiding 29 of them makes the panel jump under the
// cursor. Prefer hidden for a single field that is genuinely inert.
//
// Rules are keyed by TypeID and shared by every instance of that type; the condition receives the
// instance, so it can read sibling fields. They are presentation only — nothing here stops code
// from writing the value, and nothing here is saved to disk.

#include "Core/TypeSystem/TypeID.h"
#include "Core/TypeSystem/TypeRegistry.h"

#include <functional>
#include <string>
#include <vector>

namespace tucano {
struct PropertyInfo;
}

namespace tucano::editor {

// Tri-state: "no rule said anything" has to be distinguishable from "a rule said editable", or a
// rule for one property would silently override the metadata of every other.
enum class RuleState { Unhandled, Yes, No };

class TypeEditingRules {
public:
	// Reads the instance as a raw pointer; the typed builder below is what call sites use.
	using Condition = std::function<bool(const void* instance)>;

	// Yes  = hide this row.
	RuleState hidden(const PropertyInfo& property, const void* instance) const;
	// Yes  = draw it, but not editable.
	RuleState locked(const PropertyInfo& property, const void* instance) const;

	// Untyped registration. Property names are the C++ member names, matching PropertyInfo::name —
	// a name that matches nothing is a rule that never fires, which `unmatchedRules` reports.
	TypeEditingRules& hideUnless(const char* property, Condition when);
	TypeEditingRules& lockUnless(const char* property, Condition when);
	// The master-toggle case: everything is locked unless the condition holds, except the listed
	// properties — normally the toggle itself, which would otherwise lock itself off.
	TypeEditingRules& lockAllUnless(Condition when, std::vector<const char*> except);

	// Rules whose property name is not in `type`. A typo here fails silently at runtime, so it is
	// worth asserting in a gate rather than discovering that a field never greys out.
	std::vector<std::string> unmatchedRules(const TypeInfo& type) const;

	// The rules for a type, created on first use. Registering the same type twice adds to the same
	// object rather than replacing it.
	static TypeEditingRules& define(TypeID type);
	static const TypeEditingRules* find(TypeID type);

private:
	struct Rule {
		std::string property; // empty means "every property"
		Condition when;
		bool hides = false; // hides when false, otherwise locks
		std::vector<std::string> except;
	};

	RuleState evaluate(const PropertyInfo& property, const void* instance, bool wantHidden) const;

	std::vector<Rule> m_rules;
};

// Typed front end, so a condition is written against the struct instead of a void*.
//
//   TypeEditingRules::define<Material>()
//       .hideUnless("alphaCutoff", [](const Material& m) { return m.alphaMask; });
//
template <typename T>
class TypeRules {
public:
	explicit TypeRules(TypeEditingRules& rules) : m_rules(rules) {}

	TypeRules& hideUnless(const char* property, std::function<bool(const T&)> when) {
		m_rules.hideUnless(property, wrap(std::move(when)));
		return *this;
	}
	TypeRules& lockUnless(const char* property, std::function<bool(const T&)> when) {
		m_rules.lockUnless(property, wrap(std::move(when)));
		return *this;
	}
	TypeRules& lockAllUnless(std::function<bool(const T&)> when, std::vector<const char*> except) {
		m_rules.lockAllUnless(wrap(std::move(when)), std::move(except));
		return *this;
	}

private:
	static TypeEditingRules::Condition wrap(std::function<bool(const T&)> when) {
		return [when = std::move(when)](const void* instance) {
			return instance != nullptr && when(*static_cast<const T*>(instance));
		};
	}

	TypeEditingRules& m_rules;
};

template <typename T>
TypeRules<T> defineRules() {
	return TypeRules<T>(TypeEditingRules::define(TypeID(TypeName<T>::value)));
}

} // namespace tucano::editor
