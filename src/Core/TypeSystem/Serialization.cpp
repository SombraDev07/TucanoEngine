#include "Core/TypeSystem/Serialization.h"

#include "Core/AssetGuid.h"
#include "Core/Json.h"
#include "Core/TypeSystem/TypeInfo.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace tucano {
namespace {

// How many float components a CoreType stores contiguously, or 0 when it is not a vector.
int vectorComponents(const PropertyInfo& property) {
	switch (property.coreType) {
		case CoreType::Vec2: return 2;
		case CoreType::Vec3: return 3;
		case CoreType::Vec4:
		case CoreType::Quat: return 4;
		// Colour is stored as the vector it happens to be; the property size decides whether alpha
		// is part of it, exactly as the grid does.
		case CoreType::Color: return property.size >= sizeof(float) * 4 ? 4 : 3;
		default: return 0;
	}
}

void indentBy(std::string& out, int depth) { out.append(static_cast<size_t>(depth) * 2, ' '); }

} // namespace

void appendJsonString(std::string& out, std::string_view text) {
	out += '"';
	for (const char c : text) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					char buffer[8];
					std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
					out += buffer;
				} else {
					out += c;
				}
		}
	}
	out += '"';
}

namespace {

// %.9g and %.17g are the shortest forms that round-trip a float and a double exactly. Anything
// shorter means saving and loading a value changes it, which shows up as a scene that drifts every
// time it is opened.
void appendFloat(std::string& out, float value) {
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
	out += buffer;
}

void appendDouble(std::string& out, double value) {
	char buffer[40];
	std::snprintf(buffer, sizeof(buffer), "%.17g", value);
	out += buffer;
}

void appendInt(std::string& out, long long value) {
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%lld", value);
	out += buffer;
}

void writeValue(std::string& out, const PropertyInfo& property, const void* instance, int depth);

void writeStructBody(std::string& out, const TypeInfo& type, const void* instance, int depth);

void writeScalar(std::string& out, const PropertyInfo& property, const void* instance) {
	switch (property.coreType) {
		case CoreType::Bool: out += property.valueIn<bool>(instance) ? "true" : "false"; break;
		case CoreType::Int32: appendInt(out, property.valueIn<int32_t>(instance)); break;
		case CoreType::UInt32: appendInt(out, property.valueIn<uint32_t>(instance)); break;
		case CoreType::Int64: appendInt(out, property.valueIn<int64_t>(instance)); break;
		case CoreType::UInt64:
			appendInt(out, static_cast<long long>(property.valueIn<uint64_t>(instance)));
			break;
		case CoreType::Float: appendFloat(out, property.valueIn<float>(instance)); break;
		case CoreType::Double: appendDouble(out, property.valueIn<double>(instance)); break;
		case CoreType::String: appendJsonString(out,property.valueIn<std::string>(instance)); break;
		case CoreType::FixedString:
			// FixedString::assign always null-terminates, so reading it as a C string cannot run
			// past the property.
			appendJsonString(out,std::string(static_cast<const char*>(property.addressIn(instance))));
			break;
		case CoreType::AssetRef:
			// Hex text rather than two numbers: a reference ends up in a file people read and diff,
			// and one token is easier to grep than a pair.
			appendJsonString(out,property.valueIn<asset::AssetGuid>(instance).toString());
			break;
		default: out += "null"; break;
	}
}

void writeEnum(std::string& out, const PropertyInfo& property, const void* instance) {
	// Size-aware: an `enum class E : uint8_t` is one byte, and reading it as int32_t would pull in
	// three bytes of whatever follows.
	const int64_t value = property.enumValueIn(instance);
	const TypeInfo* enumType = TypeRegistry::instance().find(property.typeId);
	const EnumConstant* constant = enumType != nullptr ? enumType->findEnumConstant(value) : nullptr;
	// By name when the constant is known, by number when it is not. A value with no constant is
	// still worth keeping — dropping it would lose data the caller put there deliberately.
	if (constant != nullptr) {
		appendJsonString(out,constant->name);
	} else {
		appendInt(out, value);
	}
}

void writeValue(std::string& out, const PropertyInfo& property, const void* instance, int depth) {
	if (const int components = vectorComponents(property); components > 0) {
		const float* values = static_cast<const float*>(property.addressIn(instance));
		out += '[';
		for (int i = 0; i < components; ++i) {
			if (i > 0) out += ", ";
			appendFloat(out, values[i]);
		}
		out += ']';
		return;
	}

	switch (property.coreType) {
		case CoreType::Enum:
			writeEnum(out, property, instance);
			return;

		case CoreType::Struct: {
			const TypeInfo* nested = TypeRegistry::instance().find(property.typeId);
			if (nested == nullptr) {
				out += "null";
				return;
			}
			writeStructBody(out, *nested, property.addressIn(instance), depth);
			return;
		}

		case CoreType::Array: {
			// Elements share the property's metadata; only the type and stride differ.
			PropertyInfo element = property;
			element.coreType = property.elementType;
			element.arrayCount = 0;
			element.size = property.arrayCount > 0 ? property.size / property.arrayCount : property.size;

			out += '[';
			for (uint32_t i = 0; i < property.arrayCount; ++i) {
				if (i > 0) out += ", ";
				element.offset = property.offset + i * element.size;
				writeValue(out, element, instance, depth + 1);
			}
			out += ']';
			return;
		}

		default:
			writeScalar(out, property, instance);
			return;
	}
}

void writeStructBody(std::string& out, const TypeInfo& type, const void* instance, int depth) {
	out += "{\n";
	bool first = true;
	for (size_t i = 0; i < type.propertyCount; ++i) {
		const PropertyInfo& property = type.properties[i];
		// Derived state is deliberately absent from the file, not written as zero.
		if (property.meta.transient) continue;

		if (!first) out += ",\n";
		first = false;
		indentBy(out, depth + 1);
		appendJsonString(out,property.name);
		out += ": ";
		writeValue(out, property, instance, depth + 1);
	}
	out += '\n';
	indentBy(out, depth);
	out += '}';
}

// ── Reading ─────────────────────────────────────────────────────────────────

void reportSkip(std::string* err, const TypeInfo& type, const PropertyInfo& property,
                const char* expected) {
	if (err == nullptr) return;
	if (!err->empty()) *err += "; ";
	*err += std::string(type.name) + "." + property.name + ": expected " + expected;
}

void readValue(const core::JsonValue& node, const PropertyInfo& property, void* instance,
               const TypeInfo& owner, std::string* err);

void readStructBodyImpl(const core::JsonValue& node, const TypeInfo& type, void* instance,
                        std::string* err) {
	for (size_t i = 0; i < type.propertyCount; ++i) {
		const PropertyInfo& property = type.properties[i];
		if (property.meta.transient) continue;

		const core::JsonValue* field = node.find(property.name);
		// Absent means "keep what the instance already holds" — the rule that lets an old save load
		// into a struct that has grown new fields.
		if (field == nullptr) continue;
		readValue(*field, property, instance, type, err);
	}
}

void readScalar(const core::JsonValue& node, const PropertyInfo& property, void* instance,
                const TypeInfo& owner, std::string* err) {
	switch (property.coreType) {
		case CoreType::Bool:
			if (node.type != core::JsonValue::Type::Bool) return reportSkip(err, owner, property, "bool");
			property.valueIn<bool>(instance) = node.boolean;
			return;
		case CoreType::Int32:
			if (!node.isNumber()) return reportSkip(err, owner, property, "number");
			property.valueIn<int32_t>(instance) = static_cast<int32_t>(node.number);
			return;
		case CoreType::UInt32:
			if (!node.isNumber()) return reportSkip(err, owner, property, "number");
			property.valueIn<uint32_t>(instance) = static_cast<uint32_t>(node.number);
			return;
		case CoreType::Int64:
			if (!node.isNumber()) return reportSkip(err, owner, property, "number");
			property.valueIn<int64_t>(instance) = static_cast<int64_t>(node.number);
			return;
		case CoreType::UInt64:
			if (!node.isNumber()) return reportSkip(err, owner, property, "number");
			property.valueIn<uint64_t>(instance) = static_cast<uint64_t>(node.number);
			return;
		case CoreType::Float:
			if (!node.isNumber()) return reportSkip(err, owner, property, "number");
			property.valueIn<float>(instance) = static_cast<float>(node.number);
			return;
		case CoreType::Double:
			if (!node.isNumber()) return reportSkip(err, owner, property, "number");
			property.valueIn<double>(instance) = node.number;
			return;
		case CoreType::String:
			if (!node.isString()) return reportSkip(err, owner, property, "string");
			property.valueIn<std::string>(instance) = node.str;
			return;
		case CoreType::AssetRef: {
			if (!node.isString()) return reportSkip(err, owner, property, "asset id");
			const asset::AssetGuid parsed = asset::AssetGuid::fromString(node.str);
			// An unparseable id clears the reference instead of half-setting it: pointing at nothing
			// is recoverable, pointing at the wrong asset is not.
			//
			// "Unparseable" is a property of the *text*, not of the result. An all-zero id is what an
			// empty reference serialises to, and reporting that as malformed would make every scene
			// with an unassigned slot come back with a complaint.
			const bool wellFormed =
			    node.str.empty() ||
			    (node.str.size() == 32 &&
			     node.str.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos);
			if (!wellFormed) reportSkip(err, owner, property, "a 32-hex-digit asset id");
			property.valueIn<asset::AssetGuid>(instance) = parsed;
			return;
		}
		case CoreType::FixedString: {
			if (!node.isString()) return reportSkip(err, owner, property, "string");
			char* buffer = static_cast<char*>(property.addressIn(instance));
			const size_t capacity = property.size;
			const size_t n = node.str.size() < capacity - 1 ? node.str.size() : capacity - 1;
			std::memcpy(buffer, node.str.data(), n);
			buffer[n] = 0;
			// Truncation is worth reporting: the file said one thing and the object now holds
			// another, and a silent difference is how a save stops round-tripping.
			if (n != node.str.size()) reportSkip(err, owner, property, "a shorter string (truncated)");
			return;
		}
		default:
			return;
	}
}

void readEnum(const core::JsonValue& node, const PropertyInfo& property, void* instance,
              const TypeInfo& owner, std::string* err) {
	if (node.isNumber()) {
		// Numbers still load: scenes written before this enum was reflected stored the integer, and
		// refusing them would break every one of them.
		property.setEnumValueIn(instance, static_cast<int64_t>(node.number));
		return;
	}
	if (!node.isString()) return reportSkip(err, owner, property, "enum name or number");

	const TypeInfo* enumType = TypeRegistry::instance().find(property.typeId);
	if (enumType != nullptr) {
		for (size_t i = 0; i < enumType->enumConstantCount; ++i) {
			if (node.str == enumType->enumConstants[i].name) {
				property.setEnumValueIn(instance, enumType->enumConstants[i].value);
				return;
			}
		}
	}
	// A name the enum no longer has: leave the current value and say so. Guessing a number here
	// would put the object into a state nobody chose.
	reportSkip(err, owner, property, "a known enum constant");
}

void readValue(const core::JsonValue& node, const PropertyInfo& property, void* instance,
               const TypeInfo& owner, std::string* err) {
	if (const int components = vectorComponents(property); components > 0) {
		if (!node.isArray()) return reportSkip(err, owner, property, "array of numbers");
		float* values = static_cast<float*>(property.addressIn(instance));
		// Only as many components as both sides agree on. A vec4 read from a vec3 file keeps its
		// own w rather than reading past the array.
		const int count = std::min<int>(components, static_cast<int>(node.arr.size()));
		for (int i = 0; i < count; ++i) {
			if (node.arr[static_cast<size_t>(i)].isNumber()) {
				values[i] = static_cast<float>(node.arr[static_cast<size_t>(i)].number);
			}
		}
		return;
	}

	switch (property.coreType) {
		case CoreType::Enum:
			readEnum(node, property, instance, owner, err);
			return;

		case CoreType::Struct: {
			if (!node.isObject()) return reportSkip(err, owner, property, "object");
			const TypeInfo* nested = TypeRegistry::instance().find(property.typeId);
			if (nested == nullptr) return reportSkip(err, owner, property, "a registered type");
			readStructBodyImpl(node, *nested, property.addressIn(instance), err);
			return;
		}

		case CoreType::Array: {
			if (!node.isArray()) return reportSkip(err, owner, property, "array");
			PropertyInfo element = property;
			element.coreType = property.elementType;
			element.arrayCount = 0;
			element.size = property.arrayCount > 0 ? property.size / property.arrayCount : property.size;

			// Fixed-size arrays: a file with more elements is truncated, with fewer leaves the tail
			// at its current value. Neither case is worth failing the load over.
			const uint32_t count =
			    std::min<uint32_t>(property.arrayCount, static_cast<uint32_t>(node.arr.size()));
			for (uint32_t i = 0; i < count; ++i) {
				element.offset = property.offset + i * element.size;
				readValue(node.arr[i], element, instance, owner, err);
			}
			return;
		}

		default:
			readScalar(node, property, instance, owner, err);
			return;
	}
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

std::string serializeToJson(const TypeInfo& type, const void* instance, int indent) {
	std::string out;
	if (instance == nullptr) return "null";
	writeStructBody(out, type, instance, indent);
	return out;
}

bool deserializeFromJson(std::string_view json, const TypeInfo& type, void* instance,
                         std::string* err) {
	if (instance == nullptr) {
		if (err) *err = "null instance";
		return false;
	}

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

	if (err) err->clear();
	readStructBodyImpl(root, type, instance, err);
	// Field-level problems are reported but do not fail the load: one bad key should not cost the
	// rest of the scene. The caller decides whether a non-empty `err` matters.
	return true;
}

void readStructInto(const core::JsonValue& node, const TypeInfo& type, void* instance,
                    std::string* err) {
	if (instance == nullptr || !node.isObject()) return;
	readStructBodyImpl(node, type, instance, err);
}

bool writeTextAtomically(const std::string& path, const std::string& text, std::string* err) {
	// Write to a sibling temporary, then rename over the target. A crash or a full disk halfway
	// through then costs the new data, not the file that was already there.
	const std::string temporary = path + ".tmp";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file) {
			if (err) *err = "cannot open " + temporary + " for writing";
			return false;
		}
		file << text;
		if (!file) {
			if (err) *err = "write failed for " + temporary;
			return false;
		}
	}

	std::error_code ec;
	std::filesystem::rename(temporary, path, ec);
	if (ec) {
		std::filesystem::remove(temporary, ec);
		if (err) *err = "cannot replace " + path;
		return false;
	}
	return true;
}

bool readTextFile(const std::string& path, std::string& out, std::string* err) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		if (err) *err = "cannot open " + path;
		return false;
	}
	out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return true;
}

bool saveToFile(const std::string& path, const TypeInfo& type, const void* instance,
                std::string* err) {
	return writeTextAtomically(path, serializeToJson(type, instance) + "\n", err);
}

bool loadFromFile(const std::string& path, const TypeInfo& type, void* instance, std::string* err) {
	std::string text;
	if (!readTextFile(path, text, err)) return false;
	return deserializeFromJson(text, type, instance, err);
}

} // namespace tucano
