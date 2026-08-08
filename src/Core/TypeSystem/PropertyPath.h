#pragma once

#include <cstdint>
#include <string>
#include <vector>

// PropertyPath — "material.layers[2].albedo", parsed once and resolvable against an instance.
//
// Derived from Esoterica (MIT) — Code/Base/TypeSystem/PropertyPath.h
//
// A path is how anything outside the grid names a property it did not write code for: an undo entry
// that must survive a reload, a diff between two settings blocks, a console command, an animated
// parameter in a sequencer. Comparing paths as raw strings would work until the first array index,
// which is why they are parsed into elements.

namespace tucano {

class TypeRegistry;
struct TypeInfo;
struct PropertyInfo;

class PropertyPath {
public:
	struct Element {
		std::string name;
		// Index into an array property; -1 when the element is not indexed.
		int32_t index = -1;
	};

	PropertyPath() = default;
	// Accepts "a.b[3].c". A malformed path yields an empty (invalid) path rather than throwing —
	// paths often come from files and typing.
	explicit PropertyPath(const std::string& path);

	bool isValid() const { return !m_elements.empty(); }
	const std::vector<Element>& elements() const { return m_elements; }
	// Round-trips through the constructor.
	std::string toString() const;

	void append(std::string name, int32_t index = -1);
	// Drops the last element; useful for addressing the owner of a property.
	void pop();

	bool operator==(const PropertyPath& other) const;
	bool operator!=(const PropertyPath& other) const { return !(*this == other); }

	// Where a path lands: the property it names and the address of that property in the instance.
	// Both are null when the path does not exist in this type.
	struct Resolved {
		const PropertyInfo* property = nullptr;
		void* address = nullptr;
		explicit operator bool() const { return property != nullptr && address != nullptr; }
	};

	// Walks the path from `root`, stepping into nested structs through the registry.
	Resolved resolve(const TypeRegistry& registry, const TypeInfo& rootType, void* rootInstance) const;

private:
	std::vector<Element> m_elements;
};

} // namespace tucano
