#include "Core/TypeSystem/TypeRegistry.h"
#include "Core/TypeSystem/PropertyPath.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace tucano {

// ── TypeRegistry ────────────────────────────────────────────────────────────

TypeRegistry& TypeRegistry::instance() {
	// Function-local static: constructed on first use, which is the only ordering that survives
	// registrations happening during static initialisation of other translation units.
	static TypeRegistry registry;
	return registry;
}

bool TypeRegistry::registerType(const TypeInfo* info) {
	if (info == nullptr || !info->id.isValid()) return false;

	// Keep the first. A duplicate id almost always means two types share a name, and quietly
	// replacing one makes that bug invisible until something edits the wrong struct.
	if (m_types.find(info->id) != m_types.end()) return false;

	m_types.emplace(info->id, info);
	if (info->name != nullptr) m_byName.emplace(std::string_view(info->name), info);
	return true;
}

const TypeInfo* TypeRegistry::find(TypeID id) const {
	const auto it = m_types.find(id);
	return it != m_types.end() ? it->second : nullptr;
}

const TypeInfo* TypeRegistry::find(std::string_view name) const {
	const auto it = m_byName.find(name);
	return it != m_byName.end() ? it->second : nullptr;
}

void* TypeRegistry::createInstance(TypeID id) const {
	const TypeInfo* info = find(id);
	return (info != nullptr && info->create != nullptr) ? info->create() : nullptr;
}

void TypeRegistry::destroyInstance(TypeID id, void* instance) const {
	const TypeInfo* info = find(id);
	if (info != nullptr && info->destroy != nullptr && instance != nullptr) {
		info->destroy(instance);
	}
}

bool TypeRegistry::isDerivedFrom(TypeID derived, TypeID base) const {
	if (!derived.isValid() || !base.isValid()) return false;
	// A type counts as derived from itself: call sites read as "can I treat this as a Component?",
	// and a Component obviously can.
	TypeID current = derived;
	// Bounded walk: a corrupt parent chain must not hang the editor.
	for (int depth = 0; depth < 64 && current.isValid(); ++depth) {
		if (current == base) return true;
		const TypeInfo* info = find(current);
		if (info == nullptr) return false;
		current = info->parentId;
	}
	return false;
}

std::vector<const TypeInfo*> TypeRegistry::allTypes() const {
	std::vector<const TypeInfo*> out;
	out.reserve(m_types.size());
	for (const auto& [id, info] : m_types) {
		out.push_back(info);
	}
	// Sorted by name so anything that lists types (a picker, a report) is stable between runs —
	// hash map order is not.
	std::sort(out.begin(), out.end(), [](const TypeInfo* a, const TypeInfo* b) {
		return std::string_view(a->name) < std::string_view(b->name);
	});
	return out;
}

void TypeRegistry::clear() {
	m_types.clear();
	m_byName.clear();
}

// ── PropertyPath ────────────────────────────────────────────────────────────

PropertyPath::PropertyPath(const std::string& path) {
	size_t i = 0;
	while (i < path.size()) {
		// Name runs until '.' or '['.
		const size_t start = i;
		while (i < path.size() && path[i] != '.' && path[i] != '[') ++i;
		if (i == start) {
			// Empty segment ("a..b", or a leading dot): the path is malformed.
			m_elements.clear();
			return;
		}

		Element element;
		element.name = path.substr(start, i - start);

		if (i < path.size() && path[i] == '[') {
			const size_t close = path.find(']', i);
			if (close == std::string::npos) {
				m_elements.clear();
				return;
			}
			const std::string indexText = path.substr(i + 1, close - i - 1);
			if (indexText.empty() ||
			    !std::all_of(indexText.begin(), indexText.end(),
			                 [](unsigned char c) { return std::isdigit(c) != 0; })) {
				m_elements.clear();
				return;
			}
			element.index = std::atoi(indexText.c_str());
			i = close + 1;
		}

		m_elements.push_back(std::move(element));

		if (i < path.size()) {
			if (path[i] != '.') {
				m_elements.clear();
				return;
			}
			++i;
			// A trailing dot leaves nothing to parse.
			if (i == path.size()) {
				m_elements.clear();
				return;
			}
		}
	}
}

std::string PropertyPath::toString() const {
	std::string out;
	for (size_t i = 0; i < m_elements.size(); ++i) {
		if (i > 0) out += '.';
		out += m_elements[i].name;
		if (m_elements[i].index >= 0) {
			out += '[';
			out += std::to_string(m_elements[i].index);
			out += ']';
		}
	}
	return out;
}

void PropertyPath::append(std::string name, int32_t index) {
	if (name.empty()) return;
	m_elements.push_back(Element{std::move(name), index});
}

void PropertyPath::pop() {
	if (!m_elements.empty()) m_elements.pop_back();
}

bool PropertyPath::operator==(const PropertyPath& other) const {
	if (m_elements.size() != other.m_elements.size()) return false;
	for (size_t i = 0; i < m_elements.size(); ++i) {
		if (m_elements[i].name != other.m_elements[i].name ||
		    m_elements[i].index != other.m_elements[i].index) {
			return false;
		}
	}
	return true;
}

PropertyPath::Resolved PropertyPath::resolve(const TypeRegistry& registry, const TypeInfo& rootType,
                                             void* rootInstance) const {
	if (m_elements.empty() || rootInstance == nullptr) return {};

	const TypeInfo* type = &rootType;
	void* instance = rootInstance;
	const PropertyInfo* property = nullptr;

	for (size_t i = 0; i < m_elements.size(); ++i) {
		const Element& element = m_elements[i];
		if (type == nullptr) return {};

		property = type->findProperty(element.name);
		if (property == nullptr) return {};

		void* address = property->addressIn(instance);
		if (address == nullptr) return {};

		if (element.index >= 0) {
			// Indexing something that is not an array, or past its end, is a miss rather than a
			// clamp: silently editing element 0 because 7 was out of range is worse than nothing.
			if (property->coreType != CoreType::Array ||
			    static_cast<uint32_t>(element.index) >= property->arrayCount) {
				return {};
			}
			const uint32_t stride = property->arrayCount > 0 ? property->size / property->arrayCount : 0;
			if (stride == 0) return {};
			address = static_cast<uint8_t*>(address) + static_cast<size_t>(element.index) * stride;
		}

		const bool isLast = i + 1 == m_elements.size();
		if (isLast) {
			return Resolved{property, address};
		}

		// More path to walk: the current property has to be a struct we know about.
		if (property->coreType != CoreType::Struct && property->coreType != CoreType::Array) {
			return {};
		}
		type = registry.find(property->typeId);
		instance = address;
	}
	return {};
}

// Keeps the generated reflection linked in.
//
// Generated/Reflection.g.cpp registers its types from inline variables at static-init time, and
// nothing in the engine calls into that object file. In a static library that means the linker is
// free to leave it out, taking every generated registration with it — no error, no warning, just an
// editor with empty property grids. Taking the address of a symbol it defines forces it in.
//
// Anchored from here because the registry is the thing that would be empty, and because every
// program that reflects anything already links this file.
extern const int kGeneratedReflectionAnchor;
namespace {
const int* const kKeepGeneratedReflection = &kGeneratedReflectionAnchor;
} // namespace

} // namespace tucano
