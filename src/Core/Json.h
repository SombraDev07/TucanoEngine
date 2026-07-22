#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tucano::core {

// Minimal JSON value + recursive-descent parser (ECS templates, tooling configs).
class JsonValue {
public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string str;
  std::vector<JsonValue> arr;
  std::vector<std::pair<std::string, JsonValue>> obj; // insertion order preserved

  bool isObject() const { return type == Type::Object; }
  bool isArray() const { return type == Type::Array; }
  bool isString() const { return type == Type::String; }
  bool isNumber() const { return type == Type::Number; }

  const JsonValue* find(std::string_view key) const {
    for (const auto& kv : obj) {
      if (kv.first == key) {
        return &kv.second;
      }
    }
    return nullptr;
  }
  double asNumber(double def = 0.0) const { return type == Type::Number ? number : def; }
  float asFloat(float def = 0.0f) const { return type == Type::Number ? float(number) : def; }
  int asInt(int def = 0) const { return type == Type::Number ? int(number) : def; }
  bool asBool(bool def = false) const { return type == Type::Bool ? boolean : def; }
  const std::string& asString(const std::string& def = {}) const {
    return type == Type::String ? str : def;
  }

  // Parses text into out; returns false and fills err (with offset) on failure.
  static bool parse(std::string_view text, JsonValue& out, std::string* err = nullptr);
};

} // namespace tucano::core
