#include "Editor/TypeEditingRules.h"

#include "Core/TypeSystem/TypeInfo.h"
#include "Editor/EngineEditingRules.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace tucano::editor {
namespace {

std::unordered_map<TypeID, TypeEditingRules>& registry() {
	// Function-local so registration cannot race static initialisation in another translation unit.
	static std::unordered_map<TypeID, TypeEditingRules> map;
	return map;
}

} // namespace

TypeEditingRules& TypeEditingRules::hideUnless(const char* property, Condition when) {
	m_rules.push_back(Rule{property != nullptr ? property : "", std::move(when), true, {}});
	return *this;
}

TypeEditingRules& TypeEditingRules::lockUnless(const char* property, Condition when) {
	m_rules.push_back(Rule{property != nullptr ? property : "", std::move(when), false, {}});
	return *this;
}

TypeEditingRules& TypeEditingRules::lockAllUnless(Condition when, std::vector<const char*> except) {
	std::vector<std::string> exempt;
	exempt.reserve(except.size());
	for (const char* name : except) {
		if (name != nullptr) exempt.emplace_back(name);
	}
	m_rules.push_back(Rule{"", std::move(when), false, std::move(exempt)});
	return *this;
}

RuleState TypeEditingRules::evaluate(const PropertyInfo& property, const void* instance,
                                     bool wantHidden) const {
	RuleState result = RuleState::Unhandled;
	for (const Rule& rule : m_rules) {
		if (rule.hides != wantHidden) continue;
		if (!rule.property.empty() && rule.property != property.name) continue;
		if (rule.property.empty()) {
			const bool exempt = std::find(rule.except.begin(), rule.except.end(), property.name) !=
			                    rule.except.end();
			if (exempt) continue;
		}
		if (!rule.when) continue;

		// Any rule saying "yes" wins: two conditions that both gate a field mean both have to hold.
		if (!rule.when(instance)) return RuleState::Yes;
		result = RuleState::No;
	}
	return result;
}

RuleState TypeEditingRules::hidden(const PropertyInfo& property, const void* instance) const {
	return evaluate(property, instance, true);
}

RuleState TypeEditingRules::locked(const PropertyInfo& property, const void* instance) const {
	return evaluate(property, instance, false);
}

std::vector<std::string> TypeEditingRules::unmatchedRules(const TypeInfo& type) const {
	std::vector<std::string> unmatched;
	const auto declares = [&type](const std::string& name) {
		for (size_t i = 0; i < type.propertyCount; ++i) {
			if (name == type.properties[i].name) return true;
		}
		return false;
	};

	for (const Rule& rule : m_rules) {
		if (!rule.property.empty() && !declares(rule.property)) unmatched.push_back(rule.property);
		// An exemption naming a field that does not exist is the same typo with the opposite
		// effect: the property it meant to keep editable stays locked.
		for (const std::string& name : rule.except) {
			if (!declares(name)) unmatched.push_back(name);
		}
	}
	return unmatched;
}

TypeEditingRules& TypeEditingRules::define(TypeID type) { return registry()[type]; }

const TypeEditingRules* TypeEditingRules::find(TypeID type) {
	// Registered on first lookup rather than by a static initialiser in the rules file: a
	// translation unit whose only content is static initialisers can be dropped from a static
	// library by the linker, and the rules would then be silently absent. This is the same failure
	// mode as CP-20b — a fix that depends on someone remembering to call it.
	static std::once_flag once;
	std::call_once(once, [] { registerEngineEditingRules(); });

	const auto found = registry().find(type);
	return found != registry().end() ? &found->second : nullptr;
}

} // namespace tucano::editor
